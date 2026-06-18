#pragma once
#include "ga/graph/component.h"
#include "ga/graph/components/bounds_component.h"
#include "ga/graph/node.h"
#include "ga/graph/scene.h"
#include "ga/signal.h"
namespace ga {

class TouchZone : public ga::Component
{
public:
	enum class State
	{
		DISABLED = -1,  // disabled
		INACTIVE = 0,   // enabled, no touch
		ACTIVE   = 1    // enabled, touch
	};

	struct Event
	{
		enum class Type
		{
			PRESS,
			RELEASE,
			DRAG_INTO,
			DRAG_INSIDE,
			DRAG_OFF
		} type;

		TouchZone* touchZone;   // source zone (this)
		TouchEvent touchEvent;  // raw event
		vec3 localPosition;     // local touch position
		vec3 scenePosition;
	};

	void handleTouchEvent( ga::TouchEvent& touchEvt )
	{
		if ( m_state == State::DISABLED )
			return;

		if ( touchEvt.captured && m_isCapturingTouch ) {
			return;
		}

		// If this zone is owned by a different tracked finger, ignore the event.
		// touchId == -1 means untracked (mouse) — always allowed for backward compat.
		bool isTracked = ( touchEvt.touchId >= 0 );
		bool isOwnedByOther = isTracked && ( m_activeTouchId >= 0 ) && ( m_activeTouchId != touchEvt.touchId );
		if ( isOwnedByOther )
			return;

		auto node = getNode();
		if ( !node )
			return;

		// transform touchEvt position into local node position
		TouchZone::Event event;
		event.touchZone     = this;
		event.touchEvent    = touchEvt;
		event.scenePosition = ga::vec3( touchEvt.position.x, touchEvt.position.y, 0 );
		event.localPosition = node->scenePosToLocal( vec3( touchEvt.position, 0 ) );
		bool isInBounds     = m_customBoundsTest
		                          ? m_customBoundsTest( event )
		                          : node->component<Bounds>().contains( event.localPosition );  // todo: 3D bounds / ray cast...
		if ( m_boundsInverted )
			isInBounds = !isInBounds;  // e.g. for click-off

		switch ( touchEvt.type ) {
			case TouchEvent::Type::PRESS: {
				if ( isInBounds ) {
					if ( m_isCapturingTouch ) {
						touchEvt.captured = true;
					}
					// claim this touch
					m_activeTouchId = touchEvt.touchId;
					setState( State::ACTIVE );
					event.type = TouchZone::Event::Type::PRESS;
					onTouchEvent( event );
				}
				break;
			}

			case ga::TouchEvent::Type::DRAG: {
				if ( isInBounds ) {
					if ( getState() == State::ACTIVE ) {
						// This zone OWNS the touch (it received the PRESS). Only the owner
						// captures the drag, so no other zone — even one drawn on top of it
						// after the press (e.g. a card raised by an autoflip, or another
						// user's just-released card) — can steal the drag and freeze it.
						if ( m_isCapturingTouch ) {
							touchEvt.captured = true;
						}
						// dragged within bounds
						event.type = TouchZone::Event::Type::DRAG_INSIDE;
					} else {
						// Not the owner: report the drag entering (hover) but do NOT capture,
						// so the owning zone still receives this drag even when it is drawn
						// below this one.
						// setState( State::ACTIVE );
						event.type = TouchZone::Event::Type::DRAG_INTO;
					}
					onTouchEvent( event );
				} else if ( getState() == State::ACTIVE ) {
					// drag off of zone
					if ( m_allowLosingFocus ) {
						setState( State::INACTIVE );
						m_activeTouchId = -1;
					}
					event.type = TouchZone::Event::Type::DRAG_OFF;
					onTouchEvent( event );
				}
				break;
			}

			case ga::TouchEvent::Type::RELEASE: {
				if ( getState() == State::ACTIVE ) {
					// tap release
					if ( m_isCapturingTouch ) {
						touchEvt.captured = true;
					}
					setState( State::INACTIVE );
					m_activeTouchId = -1;
					event.type = TouchZone::Event::Type::RELEASE;
					onTouchEvent( event );
				} else {
					setState( State::INACTIVE );
					m_activeTouchId = -1;
				}
				break;
			}

			case ga::TouchEvent::Type::CANCEL: {
				if ( m_isCapturingTouch ) {
					touchEvt.captured = true;
				}
				setState( State::INACTIVE );
				m_activeTouchId = -1;
				break;
			}
		}
	}

	void setState( State state )
	{
		m_state = state;
	}
	State getState() const
	{
		return m_state;
	}

	inline void enable()
	{
		if ( m_state == State::DISABLED ) {
			m_state = State::INACTIVE;
		}
	}

	inline void disable()
	{
		m_state = State::DISABLED;
	}

	inline void setAllowLosingFocus( bool isAllowed = true )
	{
		m_allowLosingFocus = isAllowed;
	}

	inline void setEnableCapturingTouch( bool isEnabled = true )
	{
		m_isCapturingTouch = isEnabled;
	}

	inline bool getIsCapturingTouch()
	{
		return m_isCapturingTouch;
	}

	inline void invertBounds( bool invert = true )
	{
		m_boundsInverted = invert;
	}

	inline bool getAreBoundsInverted() const
	{
		return m_boundsInverted;
	}

	inline void setCustomBoundsTest( std::function<bool( TouchZone::Event )> testFn )
	{
		m_customBoundsTest = testFn;
	}

	// will test against ga::Bounds component attached to owner node
	inline void setUseDefaultBoundsTest()
	{
		m_customBoundsTest = nullptr;
	}

	inline bool getIsUsingCustomBoundsTest() const
	{
		return m_customBoundsTest != nullptr;
	}

	void connectTouch( std::shared_ptr<Scene> scene, int groupId )
	{
		if ( !scene )
			return;
		m_touchConnection = scene->onTouchEvent.connect_scoped( [this]( TouchEvent& te ) { handleTouchEvent( te ); }, groupId );
	}

	void disconnectTouch()
	{
		m_touchConnection.disconnect();
	}

	// signals
	ga::Signal<TouchZone::Event&> onTouchEvent;

protected:
	virtual void setScene( std::shared_ptr<Scene> scene )
	{
		disconnectTouch();
		connectTouch( scene, 0 );
	}

	State m_state;
	int m_activeTouchId                                        = -1;  // touch ID that owns this zone; -1 = none
	ga::Connection m_touchConnection;
	bool m_boundsInverted                                      = false;
	bool m_isCapturingTouch                                    = true;
	bool m_allowLosingFocus                                    = false;
	std::function<bool( TouchZone::Event )> m_customBoundsTest = nullptr;
};
}  // namespace ga
