package protocol

import (
	"bytes"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"syscall"
	"testing"
)

const goldenRequest RequestID = 0x0102030405060708
const goldenSession SessionID = 0x8899aabbccddeeff

func fixture(t testing.TB, name string) []byte {
	t.Helper()
	b, e := os.ReadFile(filepath.Join("../../../tests/protocol/v1", name+".hex"))
	if e != nil {
		t.Fatal(e)
	}
	data, e := hex.DecodeString(strings.Join(strings.Fields(string(b)), ""))
	if e != nil {
		t.Fatal(e)
	}
	return data
}
func TestGoldenAllMessages(t *testing.T) {
	epoch := [16]byte{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}
	type goldenCase struct {
		name    string
		message Message
		request RequestID
		session SessionID
		version uint16
	}
	cases := []goldenCase{
		{"hello", Hello{1, 1}, goldenRequest, 0, 0}, {"hello_ack", HelloAck{1, 65536, 0, epoch}, goldenRequest, 0, 0},
		{"create", CreateSession{24, 80, 262144}, goldenRequest, 0, 1}, {"created", SessionCreated{24, 80, 262144}, goldenRequest, goldenSession, 1},
		{"input", InputBytes{[]byte{0, 255, 27}}, goldenRequest, goldenSession, 1}, {"resize", ResizeSession{4096, 1}, goldenRequest, goldenSession, 1},
		{"close", CloseSession{}, goldenRequest, goldenSession, 1}, {"shutdown", Shutdown{}, goldenRequest, 0, 1},
		{"output", OutputBytes{0x0102030405060708, []byte{0, 255, 27}}, 0, goldenSession, 1},
		{"closed", SessionClosed{0x0102030405060708}, goldenRequest, goldenSession, 1}, {"closed_async", SessionClosed{0}, 0, goldenSession, 1},
		{"ack", Ack{TypeInputBytes}, goldenRequest, goldenSession, 1}, {"credit", OutputCredit{32768}, goldenRequest, goldenSession, 1},
		{"exited_signal", SessionExited{ExitStatus{2, 9, true}, 0, 1}, 0, goldenSession, 1},
		{"exited_unavailable", SessionExited{ExitStatus{3, 0, false}, 0, 1}, 0, goldenSession, 1},
		{"handshake_error", ErrorMessage{ErrorVersion, 0, "version"}, goldenRequest, 0, 0},
		{"error_async", ErrorMessage{ErrorInternal, 0, ""}, 0, 0, 1},
		{"error_utf8", ErrorMessage{ErrorInternal, 0, "é"}, goldenRequest, goldenSession, 1},
		{"error_max", ErrorMessage{ErrorInternal, 32768, strings.Repeat("x", 1024)}, goldenRequest, goldenSession, 1},
		{"ack_resize", Ack{TypeResizeSession}, goldenRequest, goldenSession, 1},
		{"ack_credit", Ack{TypeOutputCredit}, goldenRequest, goldenSession, 1},
		{"ack_shutdown", Ack{TypeShutdown}, goldenRequest, 0, 1},
		{"exited_signal_no_core", SessionExited{ExitStatus{2, 15, false}, 0, 7}, 0, goldenSession, 1},
		{"input_max", InputBytes{bytes.Repeat([]byte{255}, 32768)}, goldenRequest, goldenSession, 1},
		{"output_max", OutputBytes{1, make([]byte, 32768)}, 0, goldenSession, 1},
	}
	for i := 1; i <= 11; i++ {
		cases = append(cases, goldenCase{fmt.Sprintf("error_%02d", i), ErrorMessage{ErrorCode(i), 3, "error"}, goldenRequest, goldenSession, 1})
	}
	for i := 1; i <= 7; i++ {
		cases = append(cases, goldenCase{fmt.Sprintf("exited_drain_%d", i), SessionExited{ExitStatus{1, 255, false}, 0x0102030405060708, DrainReason(i)}, 0, goldenSession, 1})
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			raw := fixture(t, c.name)
			f, e := ReadFrame(bytes.NewReader(raw))
			if e != nil {
				t.Fatal(e)
			}
			m, e := Decode(f)
			if e != nil {
				t.Fatal(e)
			}
			if !reflect.DeepEqual(m, c.message) {
				t.Fatalf("decoded %#v, want %#v", m, c.message)
			}
			if f.Header.Request != c.request || f.Header.Session != c.session || f.Header.Version != c.version {
				t.Fatalf("wrong header %#v", f.Header)
			}
			encoded, e := Encode(c.message, c.request, c.session, c.version)
			if e != nil {
				t.Fatal(e)
			}
			var b bytes.Buffer
			if e = WriteFrame(&b, encoded); e != nil {
				t.Fatal(e)
			}
			if !bytes.Equal(b.Bytes(), raw) {
				t.Fatal("wire bytes differ from literal fixture")
			}
		})
	}
}
func parse(raw []byte) error {
	f, e := ReadFrame(bytes.NewReader(raw))
	if e != nil {
		return e
	}
	_, e = Decode(f)
	return e
}
func TestMalformedFrames(t *testing.T) {
	cases := []struct {
		name, fixture string
		offset        int
		value         byte
	}{
		{"magic", "input", 0, 0}, {"version", "input", 5, 2}, {"flags", "input", 9, 1}, {"reserved", "input", 11, 1}, {"type", "input", 7, 15},
		{"zero_request", "close", 23, 0}, {"zero_session", "close", 31, 0},
		{"create_rows", "create", 32, 255}, {"create_window", "create", 39, 1}, {"resize_zero", "resize", 35, 0},
		{"hello_range", "hello", 35, 0}, {"hello_ack_version", "hello_ack", 33, 2}, {"hello_ack_limit", "hello_ack", 37, 1}, {"capability", "hello_ack", 41, 1},
		{"exit_kind", "exited_signal", 32, 4}, {"exit_core_bool", "exited_signal", 37, 2}, {"exit_signal_zero", "exited_signal", 36, 0}, {"drain_kind", "exited_signal", 46, 8},
		{"unavailable_nonzero", "exited_unavailable", 36, 1}, {"exit_value", "exited_drain_1", 35, 1}, {"exit_core", "exited_drain_1", 37, 1},
		{"error_code", "error_01", 33, 12}, {"error_utf8", "error_01", 40, 255}, {"error_nul", "error_01", 40, 0}, {"error_length", "error_01", 39, 4},
		{"ack_type", "ack", 33, 7},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			b := fixture(t, c.fixture)
			b[c.offset] = c.value
			if c.name == "zero_request" {
				clear(b[16:24])
			}
			if c.name == "zero_session" {
				clear(b[24:32])
			}
			if parse(b) == nil {
				t.Fatal("accepted malformed frame")
			}
		})
	}
	for _, name := range []string{"hello", "hello_ack", "create", "created", "resize", "close", "shutdown", "closed", "exited_signal", "error_01", "ack", "credit"} {
		t.Run("trailing_"+name, func(t *testing.T) {
			b := fixture(t, name)
			b[15]++
			b = append(b, 0)
			if parse(b) == nil {
				t.Fatal("accepted trailing payload")
			}
		})
	}
	for _, name := range []string{"input", "output"} {
		t.Run("empty_"+name, func(t *testing.T) {
			b := fixture(t, name)
			size := 32
			if name == "output" {
				size = 40
			}
			b = b[:size]
			b[15] = byte(size - 32)
			if parse(b) == nil {
				t.Fatal("accepted empty terminal data")
			}
		})
	}
	t.Run("oversize_before_body", func(t *testing.T) {
		b := fixture(t, "input")[:32]
		b[12] = 0
		b[13] = 1
		b[14] = 0
		b[15] = 1
		r := &headerOnlyReader{bytes.NewReader(b), false}
		if _, e := ReadFrame(r); e == nil {
			t.Fatal("accepted oversize")
		}
		if r.bodyRead {
			t.Fatal("read body before rejecting oversize")
		}
	})
	t.Run("schema_maximum", func(t *testing.T) {
		if parse(fixture(t, "frame_max")) == nil {
			t.Fatal("accepted raw input beyond schema cap")
		}
	})
	t.Run("output_sequence_zero", func(t *testing.T) {
		b := fixture(t, "output")
		clear(b[32:40])
		if parse(b) == nil {
			t.Fatal("accepted sequence zero")
		}
	})
	t.Run("error_message_max", func(t *testing.T) {
		if _, e := Encode(ErrorMessage{ErrorInternal, 0, strings.Repeat("x", 1025)}, 1, 0, 1); e == nil {
			t.Fatal("accepted overlong error")
		}
	})
}

type headerOnlyReader struct {
	*bytes.Reader
	bodyRead bool
}

func (r *headerOnlyReader) Read(b []byte) (int, error) {
	if r.Len() == 0 {
		r.bodyRead = true
		return 0, io.EOF
	}
	return r.Reader.Read(b)
}

type fragments struct {
	data      []byte
	n         int
	interrupt bool
}

func (r *fragments) Read(p []byte) (int, error) {
	if r.interrupt {
		r.interrupt = false
		return 0, syscall.EINTR
	}
	if len(r.data) == 0 {
		return 0, io.EOF
	}
	n := min(len(p), r.n, len(r.data))
	copy(p, r.data[:n])
	r.data = r.data[n:]
	return n, nil
}

type shortWriter struct {
	bytes.Buffer
	n         int
	interrupt bool
}

func (w *shortWriter) Write(p []byte) (int, error) {
	if w.interrupt {
		w.interrupt = false
		return 0, syscall.EINTR
	}
	return w.Buffer.Write(p[:min(len(p), w.n)])
}

type noProgress struct{}

func (noProgress) Read([]byte) (int, error)  { return 0, nil }
func (noProgress) Write([]byte) (int, error) { return 0, nil }
func TestFragmentedFrames(t *testing.T) {
	raw := fixture(t, "input")
	other := fixture(t, "resize")
	for n := 1; n <= len(raw); n++ {
		r := &fragments{append(append([]byte{}, raw...), other...), n, true}
		f, e := ReadFrame(r)
		if e != nil {
			t.Fatal(e)
		}
		if !bytes.Equal(f.Body, []byte{0, 255, 27}) {
			t.Fatal("body corrupt")
		}
		f, e = ReadFrame(r)
		if e != nil {
			t.Fatal(e)
		}
		if _, e = Decode(f); e != nil {
			t.Fatal(e)
		}
		w := &shortWriter{n: n, interrupt: true}
		f, e = ReadFrame(bytes.NewReader(raw))
		if e != nil {
			t.Fatal(e)
		}
		if e = WriteFrame(w, f); e != nil {
			t.Fatal(e)
		}
		if !bytes.Equal(w.Bytes(), raw) {
			t.Fatal("short writes corrupt frame")
		}
	}
	for i := 0; i < len(raw); i++ {
		_, e := ReadFrame(bytes.NewReader(raw[:i]))
		want := io.ErrUnexpectedEOF
		if i == 0 {
			want = io.EOF
		}
		if !errors.Is(e, want) {
			t.Fatalf("EOF at %d: %v want %v", i, e, want)
		}
	}
	if _, e := ReadFrame(noProgress{}); !errors.Is(e, io.ErrNoProgress) {
		t.Fatalf("zero read: %v", e)
	}
	f, _ := ReadFrame(bytes.NewReader(raw))
	if e := WriteFrame(noProgress{}, f); !errors.Is(e, io.ErrNoProgress) {
		t.Fatalf("zero write: %v", e)
	}
	f.Header.Length++
	var b bytes.Buffer
	if e := WriteFrame(&b, f); e == nil || b.Len() != 0 {
		t.Fatal("length mismatch wrote bytes")
	}
	maximum := fixture(t, "frame_max")
	f, e := ReadFrame(bytes.NewReader(maximum))
	if e != nil || len(f.Body) != 65536 {
		t.Fatalf("maximum framing body: %v", e)
	}
}
func TestRequestRules(t *testing.T) {
	var ids RequestIDs
	for _, id := range []RequestID{1, 3, ^RequestID(0)} {
		if e := ids.Accept(id); e != nil {
			t.Fatal(e)
		}
	}
	for _, id := range []RequestID{0, 1, 3, ^RequestID(0)} {
		if e := ids.Accept(id); e == nil {
			t.Fatalf("accepted reused/zero id %d", id)
		}
	}
	for _, name := range []string{"hello", "create", "input", "resize", "close", "shutdown", "credit"} {
		f, e := ReadFrame(bytes.NewReader(fixture(t, name)))
		if e != nil {
			t.Fatal(e)
		}
		if e = ValidateDirection(f, FrontendToBackend); e != nil {
			t.Fatal(e)
		}
		if e = ValidateDirection(f, BackendToFrontend); e == nil {
			t.Fatalf("accepted reversed %s", name)
		}
	}
	for _, name := range []string{"hello_ack", "created", "output", "exited_signal", "closed", "error_01", "ack"} {
		f, _ := ReadFrame(bytes.NewReader(fixture(t, name)))
		if e := ValidateDirection(f, BackendToFrontend); e != nil {
			t.Fatal(e)
		}
		if e := ValidateDirection(f, FrontendToBackend); e == nil {
			t.Fatalf("accepted reversed %s", name)
		}
	}
	for _, name := range []string{"hello", "hello_ack", "create", "created", "input", "resize", "close", "shutdown", "ack", "credit"} {
		b := fixture(t, name)
		clear(b[16:24])
		if parse(b) == nil {
			t.Fatalf("zero request accepted %s", name)
		}
	}
	for _, name := range []string{"output", "exited_signal"} {
		b := fixture(t, name)
		b[23] = 1
		if parse(b) == nil {
			t.Fatalf("async request accepted %s", name)
		}
	}
	for _, name := range []string{"hello", "hello_ack", "create", "shutdown"} {
		b := fixture(t, name)
		b[31] = 1
		if parse(b) == nil {
			t.Fatalf("connection session accepted %s", name)
		}
	}
	for _, name := range []string{"created", "input", "resize", "close", "output", "exited_signal", "closed", "credit"} {
		b := fixture(t, name)
		clear(b[24:32])
		if parse(b) == nil {
			t.Fatalf("zero session accepted %s", name)
		}
	}
}
func FuzzReadFrame(f *testing.F) {
	for _, name := range []string{"input", "hello", "error_01", "frame_max"} {
		f.Add(fixture(f, name))
	}
	f.Fuzz(func(t *testing.T, b []byte) {
		frame, e := ReadFrame(bytes.NewReader(b))
		if e == nil {
			if len(frame.Body) > 65536 {
				t.Fatal("unbounded body")
			}
			var out bytes.Buffer
			if e := WriteFrame(&out, frame); e != nil {
				t.Fatal(e)
			}
			if !bytes.Equal(out.Bytes(), b[:32+len(frame.Body)]) {
				t.Fatal("framing changed bytes")
			}
		}
	})
}
func FuzzDecode(f *testing.F) {
	for _, name := range []string{"input", "hello", "error_01", "exited_signal"} {
		f.Add(fixture(f, name))
	}
	f.Fuzz(func(t *testing.T, b []byte) {
		frame, e := ReadFrame(bytes.NewReader(b))
		if e != nil {
			return
		}
		m, e := Decode(frame)
		if e != nil {
			return
		}
		encoded, e := Encode(m, frame.Header.Request, frame.Header.Session, frame.Header.Version)
		if e != nil {
			t.Fatal(e)
		}
		if !reflect.DeepEqual(encoded, frame) {
			t.Fatal("decode/encode changed frame")
		}
	})
}

// A credit schema restriction here would turn a recoverable accounting rejection
// into fatal contact loss. The session ledger, not Decode, owns amount validity.
func TestCreditAmountIsAccountingState(t *testing.T) {
	for _, amount := range []uint32{0, 262145, ^uint32(0)} {
		b := fixture(t, "credit")
		b[32] = byte(amount >> 24)
		b[33] = byte(amount >> 16)
		b[34] = byte(amount >> 8)
		b[35] = byte(amount)
		f, e := ReadFrame(bytes.NewReader(b))
		if e != nil {
			t.Fatal(e)
		}
		m, e := Decode(f)
		if e != nil {
			t.Fatalf("structural credit %d rejected before accounting: %v", amount, e)
		}
		if m.(OutputCredit).RawBytes != amount {
			t.Fatal("credit changed")
		}
	}
}

func TestPayloadOwnership(t *testing.T) {
	for _, name := range []string{"input", "output"} {
		f, e := ReadFrame(bytes.NewReader(fixture(t, name)))
		if e != nil {
			t.Fatal(e)
		}
		m, e := Decode(f)
		if e != nil {
			t.Fatal(e)
		}
		clear(f.Body)
		var data []byte
		switch m := m.(type) {
		case InputBytes:
			data = m.Bytes
		case OutputBytes:
			data = m.Bytes
		}
		if !bytes.Equal(data, []byte{0, 255, 27}) {
			t.Fatal("decoded payload borrowed frame storage")
		}
		f, e = Encode(m, goldenRequest, goldenSession, 1)
		if name == "output" {
			f, e = Encode(m, 0, goldenSession, 1)
		}
		if e != nil {
			t.Fatal(e)
		}
		clear(data)
		if !bytes.Equal(f.Body[len(f.Body)-3:], []byte{0, 255, 27}) {
			t.Fatal("encoded body borrowed message storage")
		}
	}
}
func TestSchemaTruncation(t *testing.T) {
	for _, name := range []string{"hello", "hello_ack", "create", "created", "resize", "output", "closed", "exited_signal", "error_01", "ack", "credit"} {
		raw := fixture(t, name)
		for i := 0; i < len(raw); i++ {
			_, e := ReadFrame(bytes.NewReader(raw[:i]))
			want := io.ErrUnexpectedEOF
			if i == 0 {
				want = io.EOF
			}
			if !errors.Is(e, want) {
				t.Fatalf("%s EOF at %d: %v", name, i, e)
			}
		}
		f, _ := ReadFrame(bytes.NewReader(raw))
		for i := 0; i < len(f.Body); i++ {
			if name == "output" && i >= 9 {
				continue
			}
			truncated := Frame{f.Header, f.Body[:i]}
			truncated.Header.Length = uint32(i)
			if _, e := Decode(truncated); e == nil {
				t.Fatalf("%s accepted truncated schema %d", name, i)
			}
		}
	}
}

func TestEncodingBoundsAndRequestWatermark(t *testing.T) {
	invalidMessages := []Message{
		InputBytes{nil}, InputBytes{make([]byte, 32769)}, OutputBytes{1, nil}, OutputBytes{1, make([]byte, 32769)},
		ErrorMessage{ErrorInternal, 32769, "error"}, ErrorMessage{ErrorInternal, 0, "\x00"}, ErrorMessage{ErrorInternal, 0, "\xff"},
	}
	for _, m := range invalidMessages {
		req := RequestID(1)
		if _, ok := m.(OutputBytes); ok {
			req = 0
		}
		if _, e := Encode(m, req, 1, 1); e == nil {
			t.Fatalf("encoded invalid %T", m)
		}
	}
	var ids RequestIDs
	if e := ids.Accept(5); e != nil {
		t.Fatal(e)
	}
	for _, id := range []RequestID{0, 4, 5} {
		if e := ids.Accept(id); e == nil {
			t.Fatalf("accepted id %d", id)
		}
	}
	if e := ids.Accept(6); e != nil {
		t.Fatal("rejected IDs changed watermark", e)
	}
}

type failingWriter struct {
	calls   int
	written int
	err     error
}

func (w *failingWriter) Write(b []byte) (int, error) {
	w.calls++
	n := min(3, len(b))
	w.written += n
	return n, w.err
}
func TestPartialWriteFailsContact(t *testing.T) {
	f, e := ReadFrame(bytes.NewReader(fixture(t, "input")))
	if e != nil {
		t.Fatal(e)
	}
	cause := errors.New("synthetic contact failure")
	w := &failingWriter{err: cause}
	if e := WriteFrame(w, f); !errors.Is(e, cause) {
		t.Fatalf("lost write error: %v", e)
	}
	if w.calls != 1 || w.written != 3 {
		t.Fatalf("continued after partial error: %#v", w)
	}
}

func TestHelloRangeIsNegotiationState(t *testing.T) {
	for _, offer := range []Hello{{0, 0}, {0, 1}, {2, 2}, {1, 2}} {
		f, e := Encode(offer, 1, 0, 0)
		if e != nil {
			t.Fatalf("ordered range %+v rejected before negotiation: %v", offer, e)
		}
		m, e := Decode(f)
		if e != nil || m != offer {
			t.Fatalf("offer changed: %#v %v", m, e)
		}
	}
}
