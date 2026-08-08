package mirrornode

import (
	"bufio"
	"context"
	"crypto/ed25519"
	"crypto/sha1"
	"encoding/base64"
	"encoding/binary"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"strconv"
	"sync"
	"testing"
	"time"
)

// acceptTestWebSocket completes a server-side RFC 6455 handshake on a hijacked
// connection and returns it. The relay side of the protocol only ever sends
// small unmasked text frames, which is exactly what these tests emit.
func acceptTestWebSocket(t *testing.T, w http.ResponseWriter, r *http.Request) net.Conn {
	t.Helper()
	if (r.Header.Get("Upgrade") != "websocket") ||
		r.Header.Get("Sec-WebSocket-Version") != "13" {
		t.Errorf("not a websocket upgrade: %v", r.Header)
	}
	digest := sha1.Sum([]byte(r.Header.Get("Sec-WebSocket-Key") + websocketGUID))
	hijacker, ok := w.(http.Hijacker)
	if !ok {
		t.Fatal("response writer is not hijackable")
	}
	conn, buffered, err := hijacker.Hijack()
	if err != nil {
		t.Fatal(err)
	}
	response := "HTTP/1.1 101 Switching Protocols\r\n" +
		"Upgrade: websocket\r\nConnection: Upgrade\r\n" +
		"Sec-WebSocket-Accept: " + base64.StdEncoding.EncodeToString(digest[:]) + "\r\n\r\n"
	if _, err := buffered.WriteString(response); err != nil {
		t.Fatal(err)
	}
	if err := buffered.Flush(); err != nil {
		t.Fatal(err)
	}
	return conn
}

func writeTestServerText(t *testing.T, conn net.Conn, payload string) {
	t.Helper()
	header := []byte{0x81}
	if len(payload) < 126 {
		header = append(header, byte(len(payload)))
	} else {
		header = append(header, 126, 0, 0)
		binary.BigEndian.PutUint16(header[2:], uint16(len(payload)))
	}
	if _, err := conn.Write(append(header, payload...)); err != nil {
		t.Errorf("server frame write: %v", err)
	}
}

// readTestClientFrame returns one client frame's opcode and unmasked payload.
func readTestClientFrame(t *testing.T, reader *bufio.Reader) (int, []byte) {
	t.Helper()
	header := make([]byte, 2)
	if _, err := io.ReadFull(reader, header); err != nil {
		t.Fatalf("client frame header: %v", err)
	}
	if header[1]&0x80 == 0 {
		t.Fatal("client frame is not masked")
	}
	length := int(header[1] & 0x7F)
	if length >= 126 {
		t.Fatalf("unexpectedly large client frame: %d", length)
	}
	mask := make([]byte, 4)
	if _, err := io.ReadFull(reader, mask); err != nil {
		t.Fatal(err)
	}
	payload := make([]byte, length)
	if _, err := io.ReadFull(reader, payload); err != nil {
		t.Fatal(err)
	}
	for i := range payload {
		payload[i] ^= mask[i%4]
	}
	return int(header[0] & 0x0F), payload
}

func testEventIdentity(t *testing.T) *Identity {
	t.Helper()
	public, private, err := ed25519.GenerateKey(nil)
	if err != nil {
		t.Fatal(err)
	}
	return &Identity{private: private, public: public}
}

func TestEventSocketAuthenticatesAndDeliversPushes(t *testing.T) {
	identity := testEventIdentity(t)
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/api/nodes/events" {
			t.Errorf("unexpected path %s", r.URL.Path)
		}
		query := r.URL.Query()
		if query.Get("owner") != "mirror9" {
			t.Errorf("unexpected owner %q", query.Get("owner"))
		}
		ts := query.Get("ts")
		if millis, err := strconv.ParseInt(ts, 10, 64); err != nil ||
			time.Since(time.UnixMilli(millis)) > time.Minute {
			t.Errorf("stale or invalid ts %q", ts)
		}
		signature, err := base64.RawURLEncoding.DecodeString(query.Get("sig"))
		canonical := []byte("forkmesh-issues-pull-v1\nmirror9\n" + ts)
		if err != nil || !ed25519.Verify(identity.public, canonical, signature) {
			t.Error("drain token signature did not verify")
		}
		conn := acceptTestWebSocket(t, w, r)
		defer conn.Close()
		writeTestServerText(t, conn,
			`{"type":"event","topic":"issues","repo":"forkmesh/forkmesh"}`)
		// Keep the socket open until the client is done with it.
		_, _ = bufio.NewReader(conn).ReadByte()
	}))
	defer server.Close()

	socket, err := NewEventSocket(server.URL, "mirror9", identity)
	if err != nil {
		t.Fatal(err)
	}
	var mu sync.Mutex
	var events []Event
	got := make(chan struct{}, 4)
	socket.Handler = func(event Event) {
		mu.Lock()
		events = append(events, event)
		mu.Unlock()
		got <- struct{}{}
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go socket.Run(ctx)
	for i := 0; i < 2; i++ {
		select {
		case <-got:
		case <-time.After(5 * time.Second):
			t.Fatal("push event did not arrive")
		}
	}
	mu.Lock()
	defer mu.Unlock()
	if len(events) < 2 || !events[0].CatchUp ||
		events[1].Topic != "issues" || events[1].Repo != "forkmesh/forkmesh" {
		t.Fatalf("unexpected events %+v", events)
	}
}

func TestEventSocketAnswersRelayPings(t *testing.T) {
	identity := testEventIdentity(t)
	pong := make(chan []byte, 1)
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		conn := acceptTestWebSocket(t, w, r)
		defer conn.Close()
		// Unmasked server ping; the client must answer with a masked pong
		// echoing the payload.
		if _, err := conn.Write([]byte{0x89, 0x02, 'h', 'i'}); err != nil {
			t.Error(err)
			return
		}
		reader := bufio.NewReader(conn)
		opcode, payload := readTestClientFrame(t, reader)
		if opcode == opcodePong {
			pong <- payload
		}
	}))
	defer server.Close()
	socket, err := NewEventSocket(server.URL, "mirror9", identity)
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go socket.Run(ctx)
	select {
	case payload := <-pong:
		if string(payload) != "hi" {
			t.Fatalf("pong payload = %q", payload)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("client never answered the ping")
	}
}

func TestEventSocketRejectsUnsafeEndpoints(t *testing.T) {
	identity := testEventIdentity(t)
	for _, endpoint := range []string{
		"http://example.test",         // plaintext to a non-loopback host
		"ftp://example.test",          // not HTTP at all
		"https://",                    // no host
		"not a url at all ://\x00bad", // unparsable
	} {
		if _, err := NewEventSocket(endpoint, "mirror9", identity); err == nil {
			t.Errorf("accepted %q", endpoint)
		}
	}
	if _, err := NewEventSocket("https://example.test", "Not-A-Node!", identity); err == nil {
		t.Error("accepted an invalid owner")
	}
	if _, err := NewEventSocket("https://example.test", "mirror9", nil); err == nil {
		t.Error("accepted a nil identity")
	}
}
