// Package session manages recording sessions and file organization.
// It implements the Year/Course/Lecture directory structure.
package session

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
)

// Session represents a recording session
type Session struct {
	ID          string    `json:"id"`
	Name        string    `json:"name"`
	Course      string    `json:"course"`
	CreatedAt   time.Time `json:"created_at"`
	Duration    int64     `json:"duration_seconds"`
	AudioFile   string    `json:"audio_file"`
	TranscriptFile string `json:"transcript_file"`
	Notes       string    `json:"notes"`
}

// Course represents an academic course
type Course struct {
	ID        string    `json:"id"`
	Name      string    `json:"name"`
	Code      string    `json:"code"`
	Year      int       `json:"year"`
	Semester  string    `json:"semester"`
	Color     string    `json:"color"`
	CreatedAt time.Time `json:"created_at"`
}

// Manager handles session and course organization
type Manager struct {
	dataDir  string
	sessions map[string]*Session
	courses  map[string]*Course
	mu       sync.RWMutex
}

// NewManager creates a new session manager
func NewManager(dataDir string) *Manager {
	m := &Manager{
		dataDir:  dataDir,
		sessions: make(map[string]*Session),
		courses:  make(map[string]*Course),
	}
	
	// Create directory structure
	os.MkdirAll(filepath.Join(dataDir, "recordings"), 0755)
	os.MkdirAll(filepath.Join(dataDir, "courses"), 0755)
	
	// Load existing data
	m.loadCourses()
	m.loadSessions()
	
	return m
}

// DataDir returns the data directory path
func (m *Manager) DataDir() string {
	return m.dataDir
}

// RecordingsDir returns the recordings directory path
func (m *Manager) RecordingsDir() string {
	return filepath.Join(m.dataDir, "recordings")
}

// CreateCourse creates a new course
func (m *Manager) CreateCourse(name, code string, year int, semester string) (*Course, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	
	id := fmt.Sprintf("%d_%s_%s", year, semester, sanitizeID(code))
	
	course := &Course{
		ID:        id,
		Name:      name,
		Code:      code,
		Year:      year,
		Semester:  semester,
		Color:     generateColor(id),
		CreatedAt: time.Now(),
	}
	
	// Create course directory
	courseDir := filepath.Join(m.dataDir, "courses", id)
	if err := os.MkdirAll(courseDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create course directory: %w", err)
	}
	
	m.courses[id] = course
	m.saveCourses()
	
	return course, nil
}

// GetCourse returns a course by ID
func (m *Manager) GetCourse(id string) (*Course, bool) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	course, ok := m.courses[id]
	return course, ok
}

// ListCourses returns all courses
func (m *Manager) ListCourses() []*Course {
	m.mu.RLock()
	defer m.mu.RUnlock()
	
	courses := make([]*Course, 0, len(m.courses))
	for _, c := range m.courses {
		courses = append(courses, c)
	}
	
	// Sort by year (desc), then semester, then name
	sort.Slice(courses, func(i, j int) bool {
		if courses[i].Year != courses[j].Year {
			return courses[i].Year > courses[j].Year
		}
		if courses[i].Semester != courses[j].Semester {
			return courses[i].Semester < courses[j].Semester
		}
		return courses[i].Name < courses[j].Name
	})
	
	return courses
}

// CreateSession creates a new recording session
func (m *Manager) CreateSession(courseID, name string) (*Session, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	
	now := time.Now()
	id := now.Format("20060102_150405")
	
	session := &Session{
		ID:        id,
		Name:      name,
		Course:    courseID,
		CreatedAt: now,
	}
	
	// Determine session directory
	var sessionDir string
	if courseID != "" {
		sessionDir = filepath.Join(m.dataDir, "courses", courseID, id)
	} else {
		sessionDir = filepath.Join(m.dataDir, "recordings", id)
	}
	
	if err := os.MkdirAll(sessionDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create session directory: %w", err)
	}
	
	session.AudioFile = filepath.Join(sessionDir, id+".wav")
	session.TranscriptFile = filepath.Join(sessionDir, id+".md")
	
	m.sessions[id] = session
	m.saveSessions()
	
	return session, nil
}

// GetSession returns a session by ID
func (m *Manager) GetSession(id string) (*Session, bool) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	session, ok := m.sessions[id]
	return session, ok
}

// ListSessions returns sessions, optionally filtered by course
func (m *Manager) ListSessions(courseID string) []*Session {
	m.mu.RLock()
	defer m.mu.RUnlock()
	
	sessions := make([]*Session, 0)
	for _, s := range m.sessions {
		if courseID == "" || s.Course == courseID {
			sessions = append(sessions, s)
		}
	}
	
	// Sort by creation time (newest first)
	sort.Slice(sessions, func(i, j int) bool {
		return sessions[i].CreatedAt.After(sessions[j].CreatedAt)
	})
	
	return sessions
}

// UpdateSession updates a session
func (m *Manager) UpdateSession(session *Session) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	
	m.sessions[session.ID] = session
	return m.saveSessions()
}

// GetSessionDir returns the directory for a session
func (m *Manager) GetSessionDir(courseID, sessionID string) string {
	if courseID != "" {
		return filepath.Join(m.dataDir, "courses", courseID, sessionID)
	}
	return filepath.Join(m.dataDir, "recordings", sessionID)
}

// loadCourses loads courses from disk
func (m *Manager) loadCourses() error {
	path := filepath.Join(m.dataDir, "courses.json")
	data, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}
	
	var courses []*Course
	if err := json.Unmarshal(data, &courses); err != nil {
		return err
	}
	
	for _, c := range courses {
		m.courses[c.ID] = c
	}
	
	return nil
}

// saveCourses saves courses to disk
func (m *Manager) saveCourses() error {
	courses := make([]*Course, 0, len(m.courses))
	for _, c := range m.courses {
		courses = append(courses, c)
	}
	
	data, err := json.MarshalIndent(courses, "", "  ")
	if err != nil {
		return err
	}
	
	path := filepath.Join(m.dataDir, "courses.json")
	return os.WriteFile(path, data, 0644)
}

// loadSessions loads sessions from disk
func (m *Manager) loadSessions() error {
	path := filepath.Join(m.dataDir, "sessions.json")
	data, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}
	
	var sessions []*Session
	if err := json.Unmarshal(data, &sessions); err != nil {
		return err
	}
	
	for _, s := range sessions {
		m.sessions[s.ID] = s
	}
	
	return nil
}

// saveSessions saves sessions to disk
func (m *Manager) saveSessions() error {
	sessions := make([]*Session, 0, len(m.sessions))
	for _, s := range m.sessions {
		sessions = append(sessions, s)
	}
	
	data, err := json.MarshalIndent(sessions, "", "  ")
	if err != nil {
		return err
	}
	
	path := filepath.Join(m.dataDir, "sessions.json")
	return os.WriteFile(path, data, 0644)
}

// sanitizeID removes special characters from an ID string
func sanitizeID(s string) string {
	s = strings.ToLower(s)
	s = strings.ReplaceAll(s, " ", "_")
	s = strings.ReplaceAll(s, "-", "_")
	
	var result strings.Builder
	for _, r := range s {
		if (r >= 'a' && r <= 'z') || (r >= '0' && r <= '9') || r == '_' {
			result.WriteRune(r)
		}
	}
	return result.String()
}

// generateColor generates a consistent color for an ID
func generateColor(id string) string {
	colors := []string{
		"#3B82F6", // blue
		"#10B981", // green
		"#F59E0B", // amber
		"#EF4444", // red
		"#8B5CF6", // purple
		"#EC4899", // pink
		"#06B6D4", // cyan
		"#F97316", // orange
	}
	
	hash := 0
	for _, c := range id {
		hash = int(c) + ((hash << 5) - hash)
	}
	if hash < 0 {
		hash = -hash
	}
	
	return colors[hash%len(colors)]
}
