package session

import (
	"os"
	"path/filepath"
	"testing"
)

func TestNewManager(t *testing.T) {
	tmpDir := t.TempDir()
	
	m := NewManager(tmpDir)
	
	if m == nil {
		t.Fatal("NewManager returned nil")
	}
	
	// Check directories were created
	if _, err := os.Stat(filepath.Join(tmpDir, "recordings")); os.IsNotExist(err) {
		t.Error("recordings directory was not created")
	}
	
	if _, err := os.Stat(filepath.Join(tmpDir, "courses")); os.IsNotExist(err) {
		t.Error("courses directory was not created")
	}
}

func TestCreateCourse(t *testing.T) {
	tmpDir := t.TempDir()
	m := NewManager(tmpDir)
	
	course, err := m.CreateCourse("Introduction to CS", "CS101", 2024, "Fall")
	
	if err != nil {
		t.Fatalf("CreateCourse failed: %v", err)
	}
	
	if course.Name != "Introduction to CS" {
		t.Errorf("Expected name 'Introduction to CS', got '%s'", course.Name)
	}
	
	if course.Code != "CS101" {
		t.Errorf("Expected code 'CS101', got '%s'", course.Code)
	}
	
	if course.Year != 2024 {
		t.Errorf("Expected year 2024, got %d", course.Year)
	}
	
	if course.Semester != "Fall" {
		t.Errorf("Expected semester 'Fall', got '%s'", course.Semester)
	}
	
	// Verify course directory was created
	courseDir := filepath.Join(tmpDir, "courses", course.ID)
	if _, err := os.Stat(courseDir); os.IsNotExist(err) {
		t.Error("Course directory was not created")
	}
}

func TestCreateSession(t *testing.T) {
	tmpDir := t.TempDir()
	m := NewManager(tmpDir)
	
	// Create without course
	sess, err := m.CreateSession("", "Test Lecture")
	
	if err != nil {
		t.Fatalf("CreateSession failed: %v", err)
	}
	
	if sess.Name != "Test Lecture" {
		t.Errorf("Expected name 'Test Lecture', got '%s'", sess.Name)
	}
	
	if sess.Course != "" {
		t.Errorf("Expected empty course, got '%s'", sess.Course)
	}
	
	// Create with course
	course, _ := m.CreateCourse("Test Course", "TC100", 2024, "Spring")
	sess2, err := m.CreateSession(course.ID, "Lecture 1")
	
	if err != nil {
		t.Fatalf("CreateSession with course failed: %v", err)
	}
	
	if sess2.Course != course.ID {
		t.Errorf("Expected course '%s', got '%s'", course.ID, sess2.Course)
	}
}

func TestListCourses(t *testing.T) {
	tmpDir := t.TempDir()
	m := NewManager(tmpDir)
	
	// Create multiple courses
	m.CreateCourse("Course A", "CA100", 2024, "Fall")
	m.CreateCourse("Course B", "CB200", 2024, "Spring")
	m.CreateCourse("Course C", "CC300", 2023, "Fall")
	
	courses := m.ListCourses()
	
	if len(courses) != 3 {
		t.Errorf("Expected 3 courses, got %d", len(courses))
	}
	
	// Should be sorted by year desc, then semester, then name
	// 2024 Fall, 2024 Spring, 2023 Fall
	if courses[0].Code != "CA100" {
		t.Errorf("Expected first course to be CA100, got %s", courses[0].Code)
	}
}

func TestListSessions(t *testing.T) {
	tmpDir := t.TempDir()
	m := NewManager(tmpDir)
	
	course, _ := m.CreateCourse("Test", "T100", 2024, "Fall")
	
	m.CreateSession(course.ID, "Session 1")
	m.CreateSession(course.ID, "Session 2")
	m.CreateSession("", "Session 3")  // No course
	
	// All sessions
	all := m.ListSessions("")
	if len(all) != 3 {
		t.Errorf("Expected 3 sessions, got %d", len(all))
	}
	
	// Course sessions only
	courseSessions := m.ListSessions(course.ID)
	if len(courseSessions) != 2 {
		t.Errorf("Expected 2 course sessions, got %d", len(courseSessions))
	}
}

func TestSanitizeID(t *testing.T) {
	tests := []struct {
		input    string
		expected string
	}{
		{"CS101", "cs101"},
		{"My Course", "my_course"},
		{"Test-123", "test_123"},
		{"Special!@#$%", "special"},
	}
	
	for _, test := range tests {
		result := sanitizeID(test.input)
		if result != test.expected {
			t.Errorf("sanitizeID(%q) = %q, expected %q", test.input, result, test.expected)
		}
	}
}
