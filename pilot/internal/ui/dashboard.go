// Package ui provides the Fyne-based user interface components.
package ui

import (
	"fmt"
	"image/color"
	"time"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/canvas"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/dialog"
	"fyne.io/fyne/v2/theme"
	"fyne.io/fyne/v2/widget"

	"github.com/topnotchnotes/pilot/internal/ipc"
	"github.com/topnotchnotes/pilot/internal/session"
)

// Dashboard is the main UI component
type Dashboard struct {
	controller  *ipc.Controller
	sessManager *session.Manager

	// UI Components
	toolbar   fyne.CanvasObject
	statusBar *fyne.Container
	sidebar   fyne.CanvasObject
	mainArea  fyne.CanvasObject

	// Toolbar buttons (we use widget.Button for enable/disable control)
	recordBtn *widget.Button
	stopBtn   *widget.Button
	pauseBtn  *widget.Button

	// Text areas
	transcriptText *widget.Entry
	notesText      *widget.Entry

	// Level meter
	levelBar   *widget.ProgressBar
	levelLabel *widget.Label

	// Status
	statusLabel   *widget.Label
	durationLabel *widget.Label

	// Session list
	sessionList *widget.List
	courseList  *widget.List

	// Cached data for list updates
	cachedCourses  []*session.Course
	cachedSessions []*session.Session

	// State
	recordingStart time.Time
	currentSession *session.Session
	selectedCourse string
}

// NewDashboard creates a new dashboard UI
func NewDashboard(controller *ipc.Controller, sessManager *session.Manager) *Dashboard {
	d := &Dashboard{
		controller:  controller,
		sessManager: sessManager,
	}

	d.buildUI()
	d.setupEventHandlers()

	return d
}

// buildUI constructs all UI components
func (d *Dashboard) buildUI() {
	// Toolbar buttons
	d.recordBtn = widget.NewButtonWithIcon("Record", theme.MediaRecordIcon(), d.onRecord)
	d.stopBtn = widget.NewButtonWithIcon("Stop", theme.MediaStopIcon(), d.onStop)
	d.stopBtn.Disable()
	d.pauseBtn = widget.NewButtonWithIcon("Pause", theme.MediaPauseIcon(), d.onPause)
	d.pauseBtn.Disable()

	newCourseBtn := widget.NewButtonWithIcon("", theme.FolderNewIcon(), d.onNewCourse)

	toolbarBox := container.NewHBox(
		d.recordBtn,
		d.stopBtn,
		d.pauseBtn,
		widget.NewSeparator(),
		newCourseBtn,
	)
	d.toolbar = toolbarBox

	// Status bar
	d.statusLabel = widget.NewLabel("Ready")
	d.durationLabel = widget.NewLabel("00:00:00")
	d.levelBar = widget.NewProgressBar()
	d.levelBar.Min = -60
	d.levelBar.Max = 0
	d.levelBar.SetValue(-60)
	d.levelLabel = widget.NewLabel("-∞ dB")

	d.statusBar = container.NewBorder(
		nil, nil,
		container.NewHBox(
			widget.NewIcon(theme.MediaRecordIcon()),
			d.statusLabel,
			widget.NewSeparator(),
			d.durationLabel,
		),
		container.NewHBox(
			d.levelLabel,
		),
		d.levelBar,
	)

	// Sidebar - Course and Session navigation
	d.buildSidebar()

	// Main area - Transcript and notes
	d.buildMainArea()
}

// buildSidebar constructs the navigation sidebar
func (d *Dashboard) buildSidebar() {
	// Initialize cached data
	d.cachedCourses = d.sessManager.ListCourses()
	d.cachedSessions = d.sessManager.ListSessions("")

	// Course list
	d.courseList = widget.NewList(
		func() int { return len(d.cachedCourses) + 1 }, // +1 for "All Sessions"
		func() fyne.CanvasObject {
			return container.NewHBox(
				canvas.NewCircle(theme.PrimaryColor()),
				widget.NewLabel("Course Name"),
			)
		},
		func(id widget.ListItemID, obj fyne.CanvasObject) {
			box := obj.(*fyne.Container)
			circle := box.Objects[0].(*canvas.Circle)
			label := box.Objects[1].(*widget.Label)

			if id == 0 {
				circle.FillColor = theme.ForegroundColor()
				label.SetText("All Sessions")
			} else if int(id)-1 < len(d.cachedCourses) {
				c := d.cachedCourses[int(id)-1]
				circle.FillColor = parseColor(c.Color)
				label.SetText(c.Name)
			}
		},
	)

	d.courseList.OnSelected = func(id widget.ListItemID) {
		if id == 0 {
			d.selectedCourse = ""
		} else if int(id)-1 < len(d.cachedCourses) {
			d.selectedCourse = d.cachedCourses[int(id)-1].ID
		}
		d.refreshSessionList()
	}

	// Session list
	d.sessionList = widget.NewList(
		func() int { return len(d.cachedSessions) },
		func() fyne.CanvasObject {
			return container.NewVBox(
				widget.NewLabel("Session Name"),
				widget.NewLabel("Date"),
			)
		},
		func(id widget.ListItemID, obj fyne.CanvasObject) {
			if int(id) < len(d.cachedSessions) {
				s := d.cachedSessions[int(id)]
				box := obj.(*fyne.Container)
				nameLabel := box.Objects[0].(*widget.Label)
				dateLabel := box.Objects[1].(*widget.Label)

				name := s.Name
				if name == "" {
					name = s.ID
				}
				nameLabel.SetText(name)
				dateLabel.SetText(s.CreatedAt.Format("Jan 02, 2006 15:04"))
			}
		},
	)

	d.sessionList.OnSelected = func(id widget.ListItemID) {
		if int(id) < len(d.cachedSessions) {
			d.loadSession(d.cachedSessions[int(id)])
		}
	}

	// Build sidebar layout
	courseLabel := widget.NewLabelWithStyle("Courses", fyne.TextAlignCenter, fyne.TextStyle{Bold: true})
	sessionLabel := widget.NewLabelWithStyle("Sessions", fyne.TextAlignCenter, fyne.TextStyle{Bold: true})

	d.sidebar = container.NewVSplit(
		container.NewBorder(courseLabel, nil, nil, nil, d.courseList),
		container.NewBorder(sessionLabel, nil, nil, nil, d.sessionList),
	)
}

// buildMainArea constructs the main content area
func (d *Dashboard) buildMainArea() {
	// Transcript view (read-only, shows live transcription)
	d.transcriptText = widget.NewMultiLineEntry()
	d.transcriptText.Wrapping = fyne.TextWrapWord
	d.transcriptText.SetPlaceHolder("Transcription will appear here during recording...")

	transcriptCard := widget.NewCard("Live Transcript", "", d.transcriptText)

	// Notes editor
	d.notesText = widget.NewMultiLineEntry()
	d.notesText.Wrapping = fyne.TextWrapWord
	d.notesText.SetPlaceHolder("Add your notes here...")

	notesCard := widget.NewCard("Notes", "", d.notesText)

	// Split view
	d.mainArea = container.NewVSplit(
		transcriptCard,
		notesCard,
	)
}

// setupEventHandlers registers handlers for harness events
func (d *Dashboard) setupEventHandlers() {
	d.controller.OnEvent(func(event ipc.TelemetryEvent) {
		switch event.Event {
		case ipc.EventText:
			// Append transcribed text
			current := d.transcriptText.Text
			if current != "" {
				current += " "
			}
			d.transcriptText.SetText(current + event.Body)
			
		case ipc.EventLevel:
			// Update level meter
			d.levelBar.SetValue(event.DB)
			d.levelLabel.SetText(fmt.Sprintf("%.1f dB", event.DB))
			
		case ipc.EventStatus:
			d.statusLabel.SetText(event.State)
			d.updateButtonStates(event.State)
			
		case ipc.EventError:
			d.ShowWarning(event.Body)
			
		case ipc.EventSession:
			if event.Action == "start" {
				d.recordingStart = time.Now()
				go d.updateDuration()
			}
		}
	})
}

// updateDuration updates the duration display during recording
func (d *Dashboard) updateDuration() {
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()

	for range ticker.C {
		if !d.controller.IsRecording() {
			return
		}
		
		duration := time.Since(d.recordingStart)
		hours := int(duration.Hours())
		mins := int(duration.Minutes()) % 60
		secs := int(duration.Seconds()) % 60
		
		d.durationLabel.SetText(fmt.Sprintf("%02d:%02d:%02d", hours, mins, secs))
	}
}

// updateButtonStates updates toolbar button states based on recording state
func (d *Dashboard) updateButtonStates(state string) {
	switch state {
	case "recording":
		d.recordBtn.Disable()
		d.stopBtn.Enable()
		d.pauseBtn.Enable()
	case "paused":
		d.recordBtn.Enable()
		d.stopBtn.Enable()
		d.pauseBtn.Disable()
	default: // idle, ready
		d.recordBtn.Enable()
		d.stopBtn.Disable()
		d.pauseBtn.Disable()
	}
}

// Event handlers
func (d *Dashboard) onRecord() {
	// Create a new session
	sess, err := d.sessManager.CreateSession(d.selectedCourse, "")
	if err != nil {
		d.ShowWarning("Failed to create session: " + err.Error())
		return
	}
	
	d.currentSession = sess
	d.controller.SetOutputDir(d.sessManager.GetSessionDir(d.selectedCourse, sess.ID))
	
	// Clear transcript
	d.transcriptText.SetText("")
	
	// Start recording
	if err := d.controller.StartRecording(); err != nil {
		d.ShowWarning("Failed to start recording: " + err.Error())
	}
}

func (d *Dashboard) onStop() {
	if err := d.controller.Stop(); err != nil {
		d.ShowWarning("Failed to stop recording: " + err.Error())
	}
	
	// Save transcript to session
	if d.currentSession != nil {
		d.currentSession.Notes = d.notesText.Text
		d.sessManager.UpdateSession(d.currentSession)
	}
	
	d.refreshSessionList()
}

func (d *Dashboard) onPause() {
	if d.controller.State() == "recording" {
		if err := d.controller.Pause(); err != nil {
			d.ShowWarning("Failed to pause recording: " + err.Error())
		}
	} else {
		if err := d.controller.Resume(); err != nil {
			d.ShowWarning("Failed to resume recording: " + err.Error())
		}
	}
}

func (d *Dashboard) onNewCourse() {
	// Show dialog for new course
	nameEntry := widget.NewEntry()
	nameEntry.SetPlaceHolder("Course Name")
	
	codeEntry := widget.NewEntry()
	codeEntry.SetPlaceHolder("Course Code (e.g., CS101)")
	
	yearEntry := widget.NewEntry()
	yearEntry.SetPlaceHolder("Year")
	yearEntry.SetText(fmt.Sprintf("%d", time.Now().Year()))
	
	semesterSelect := widget.NewSelect(
		[]string{"Spring", "Summer", "Fall", "Winter"},
		func(s string) {},
	)
	semesterSelect.SetSelected("Fall")
	
	formItems := []*widget.FormItem{
		{Text: "Name", Widget: nameEntry},
		{Text: "Code", Widget: codeEntry},
		{Text: "Year", Widget: yearEntry},
		{Text: "Semester", Widget: semesterSelect},
	}
	
	dialog.ShowForm("New Course", "Create", "Cancel", formItems,
		func(confirmed bool) {
			if confirmed {
				year := 2024
				fmt.Sscanf(yearEntry.Text, "%d", &year)
				
				_, err := d.sessManager.CreateCourse(
					nameEntry.Text,
					codeEntry.Text,
					year,
					semesterSelect.Selected,
				)
				if err != nil {
					d.ShowWarning("Failed to create course: " + err.Error())
				}
				d.refreshCourseList()
			}
		}, fyne.CurrentApp().Driver().AllWindows()[0])
}

func (d *Dashboard) loadSession(sess *session.Session) {
	d.currentSession = sess
	d.notesText.SetText(sess.Notes)
	// TODO: Load transcript from file
}

func (d *Dashboard) refreshSessionList() {
	d.cachedSessions = d.sessManager.ListSessions(d.selectedCourse)
	d.sessionList.Refresh()
}

func (d *Dashboard) refreshCourseList() {
	d.cachedCourses = d.sessManager.ListCourses()
	d.courseList.Refresh()
}

// ShowWarning displays a warning message
func (d *Dashboard) ShowWarning(message string) {
	windows := fyne.CurrentApp().Driver().AllWindows()
	if len(windows) > 0 {
		dialog.ShowInformation("Warning", message, windows[0])
	}
}

// Accessor methods for layout
func (d *Dashboard) Toolbar() fyne.CanvasObject   { return d.toolbar }
func (d *Dashboard) StatusBar() fyne.CanvasObject { return d.statusBar }
func (d *Dashboard) Sidebar() fyne.CanvasObject   { return d.sidebar }
func (d *Dashboard) MainArea() fyne.CanvasObject  { return d.mainArea }

// parseColor parses a hex color string
func parseColor(hex string) color.Color {
	if len(hex) != 7 || hex[0] != '#' {
		return theme.PrimaryColor()
	}
	
	var r, g, b uint8
	fmt.Sscanf(hex[1:], "%02x%02x%02x", &r, &g, &b)
	
	return color.RGBA{R: r, G: g, B: b, A: 255}
}
