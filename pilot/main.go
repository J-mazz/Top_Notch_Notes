// Package main is the entry point for the TopNotchNotes Pilot application.
// The Pilot is the Go/Fyne-based frontend that provides the user interface
// and orchestrates the C++ Harness backend.
package main

import (
	"log"
	"os"
	"path/filepath"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/app"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/theme"

	"github.com/topnotchnotes/pilot/internal/ipc"
	"github.com/topnotchnotes/pilot/internal/session"
	"github.com/topnotchnotes/pilot/internal/ui"
)

const (
	appID      = "com.topnotchnotes.pilot"
	appName    = "TopNotchNotes"
	appVersion = "1.0.0"
)

func main() {
	// Initialize the Fyne application
	a := app.NewWithID(appID)
	a.SetIcon(theme.DocumentIcon())

	// Create the main window
	w := a.NewWindow(appName)
	w.Resize(fyne.NewSize(1200, 800))
	w.CenterOnScreen()

	// Determine harness binary path
	harnessPath := findHarnessBinary()

	// Initialize the session manager
	sessManager := session.NewManager(getDataDir())

	// Initialize the process controller (IPC with C++ harness)
	controller := ipc.NewController(harnessPath)

	// Create the main dashboard UI
	dashboard := ui.NewDashboard(controller, sessManager)

	// Build the main layout
	mainContent := container.NewBorder(
		dashboard.Toolbar(),  // Top: Toolbar with record/stop buttons
		dashboard.StatusBar(), // Bottom: Status bar with level meter
		dashboard.Sidebar(),   // Left: Course/session navigation
		nil,                   // Right: None
		dashboard.MainArea(),  // Center: Text editor and transcript view
	)

	w.SetContent(mainContent)

	// Handle window close
	w.SetCloseIntercept(func() {
		// Stop recording if active
		if controller.IsRecording() {
			if err := controller.Stop(); err != nil {
				log.Printf("Error stopping recording: %v", err)
			}
		}

		// Terminate the harness process
		controller.Terminate()

		// Close the window
		w.Close()
	})

	// Start the harness process
	if err := controller.Start(); err != nil {
		log.Printf("Warning: Could not start harness: %v", err)
		dashboard.ShowWarning("Harness not available. Recording will be simulated.")
	}

	// Run the application
	w.ShowAndRun()
}

// findHarnessBinary locates the C++ harness binary
func findHarnessBinary() string {
	// Check common locations
	candidates := []string{
		"./harness",
		"./bin/harness",
		"../harness/build/harness",
		"/usr/local/bin/topnotch-harness",
	}

	execPath, err := os.Executable()
	if err == nil {
		execDir := filepath.Dir(execPath)
		candidates = append([]string{
			filepath.Join(execDir, "harness"),
			filepath.Join(execDir, "..", "harness", "build", "harness"),
		}, candidates...)
	}

	for _, path := range candidates {
		if _, err := os.Stat(path); err == nil {
			return path
		}
	}

	// Return default, will fail gracefully at runtime
	return "./harness"
}

// getDataDir returns the application data directory
func getDataDir() string {
	configDir, err := os.UserConfigDir()
	if err != nil {
		configDir = os.TempDir()
	}
	dataDir := filepath.Join(configDir, "TopNotchNotes")
	os.MkdirAll(dataDir, 0755)
	return dataDir
}
