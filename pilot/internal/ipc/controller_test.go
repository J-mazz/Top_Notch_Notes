package ipc

import (
	"testing"
)

func TestNewController(t *testing.T) {
	c := NewController("/path/to/harness")
	
	if c == nil {
		t.Fatal("NewController returned nil")
	}
	
	if c.State() != "idle" {
		t.Errorf("Expected initial state 'idle', got '%s'", c.State())
	}
	
	if c.IsRecording() {
		t.Error("Expected IsRecording to be false initially")
	}
}

func TestEventHandler(t *testing.T) {
	c := NewController("/path/to/harness")
	
	eventReceived := false
	c.OnEvent(func(event TelemetryEvent) {
		eventReceived = true
		if event.Event != EventStatus {
			t.Errorf("Expected event type 'status', got '%s'", event.Event)
		}
	})
	
	// Simulate processing an event
	c.processEvent(TelemetryEvent{
		Event: EventStatus,
		State: "recording",
	})
	
	if !c.IsRecording() {
		t.Error("Expected IsRecording to be true after 'recording' status")
	}
	
	if c.State() != "recording" {
		t.Errorf("Expected state 'recording', got '%s'", c.State())
	}
}

func TestLevelTracking(t *testing.T) {
	c := NewController("/path/to/harness")
	
	c.processEvent(TelemetryEvent{
		Event: EventLevel,
		DB:    -12.5,
	})
	
	if c.LastLevel() != -12.5 {
		t.Errorf("Expected level -12.5, got %f", c.LastLevel())
	}
}

func TestSessionTracking(t *testing.T) {
	c := NewController("/path/to/harness")
	
	// Session start
	c.processEvent(TelemetryEvent{
		Event:  EventSession,
		Action: "start",
		ID:     "20240105_143022",
	})
	
	if c.SessionID() != "20240105_143022" {
		t.Errorf("Expected session ID '20240105_143022', got '%s'", c.SessionID())
	}
	
	// Session end
	c.processEvent(TelemetryEvent{
		Event:  EventSession,
		Action: "end",
	})
	
	if c.SessionID() != "" {
		t.Errorf("Expected empty session ID after end, got '%s'", c.SessionID())
	}
}
