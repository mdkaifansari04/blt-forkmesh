package mirrornode

import (
	"bufio"
	"context"
	"crypto/rand"
	"crypto/sha1"
	"crypto/tls"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"
)

// EventSocket carries payload-free relay events over one authenticated
// WebSocket. Reconnects emit a catch-up event, so no fallback poll is needed.
// Repository data and commands remain on signed HTTPS routes.
type EventSocket struct {
	endpoint string
	owner    string
	identity *Identity

	// Handler receives relay pushes and one synthetic catch-up after each connect.
	Handler   func(Event)
	Connected func(bool)

	tlsConfig    *tls.Config
	dial         func(ctx context.Context, network, address string) (net.Conn, error)
	pingInterval time.Duration
	idleTimeout  time.Duration
	minBackoff   time.Duration
	maxBackoff   time.Duration
}

// Event is one payload-free relay push.
type Event struct {
	Topic   string
	Repo    string
	CatchUp bool
}

const (
	websocketGUID       = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
	eventMaxFrameBytes  = 16 << 10
	eventHandshakeLimit = 32 << 10
)

// NewEventSocket builds the authenticated channel for one owner account.
func NewEventSocket(catalogURL, owner string, identity *Identity) (*EventSocket, error) {
	base, err := url.Parse(catalogURL)
	if err != nil || base.Host == "" {
		return nil, errors.New("catalogUrl must be an absolute URL")
	}
	// Plain ws:// is accepted only for a loopback relay so the tests can run a
	// real handshake; production is always wss:// over the public relay.
	loopback := base.Scheme == "http" && isLoopbackHost(base.Hostname())
	if base.Scheme != "https" && !loopback {
		return nil, errors.New("catalogUrl must be an absolute HTTPS URL")
	}
	if !nodePattern.MatchString(owner) {
		return nil, errors.New("event socket owner is invalid")
	}
	if identity == nil {
		return nil, errors.New("event socket requires an identity")
	}
	scheme := "wss"
	if loopback {
		scheme = "ws"
	}
	endpoint := (&url.URL{Scheme: scheme, Host: base.Host, Path: "/api/nodes/events"}).String()
	return &EventSocket{
		endpoint:     endpoint,
		owner:        owner,
		identity:     identity,
		pingInterval: 4 * time.Minute,
		idleTimeout:  9 * time.Minute,
		minBackoff:   time.Second,
		maxBackoff:   time.Minute,
	}, nil
}

func isLoopbackHost(host string) bool {
	return host == "127.0.0.1" || host == "localhost" || host == "::1"
}

// Run keeps the channel connected until ctx is done.
func (s *EventSocket) Run(ctx context.Context) {
	backoff := s.minBackoff
	for ctx.Err() == nil {
		start := time.Now()
		if err := s.serve(ctx); err != nil && ctx.Err() == nil {
			log.Printf("node event channel: %v", err)
		}
		if time.Since(start) >= 2*s.pingInterval {
			backoff = s.minBackoff
		}
		select {
		case <-ctx.Done():
			return
		case <-time.After(backoff):
		}
		if backoff *= 2; backoff > s.maxBackoff {
			backoff = s.maxBackoff
		}
	}
}

// signedURL mints a fresh forkmesh-issues-pull-v1 drain token per attempt —
// the same canonical the inbox GET/DELETE routes and GET /api/sync verify, so
// the channel grants no authority this node does not already hold.
func (s *EventSocket) signedURL() string {
	timestamp := strconv.FormatInt(time.Now().UnixMilli(), 10)
	canonical := "forkmesh-issues-pull-v1\n" + s.owner + "\n" + timestamp
	query := url.Values{}
	query.Set("owner", s.owner)
	query.Set("ts", timestamp)
	query.Set("sig", s.identity.Sign([]byte(canonical)))
	return s.endpoint + "?" + query.Encode()
}

func (s *EventSocket) serve(ctx context.Context) error {
	conn, reader, err := s.connect(ctx)
	if err != nil {
		return err
	}
	defer conn.Close()
	// Close the connection as soon as the daemon stops so the read below
	// unblocks; the hand-rolled framing has no other cancellation point.
	done := make(chan struct{})
	defer close(done)
	go func() {
		select {
		case <-ctx.Done():
			_ = conn.Close()
		case <-done:
		}
	}()

	s.setConnected(true)
	defer s.setConnected(false)
	s.emit(Event{CatchUp: true})

	pings := time.NewTicker(s.pingInterval)
	defer pings.Stop()
	writeErr := make(chan error, 1)
	go func() {
		for {
			select {
			case <-done:
				return
			case <-pings.C:
				if err := writeFrame(conn, opcodeText, []byte(`{"type":"ping"}`)); err != nil {
					select {
					case writeErr <- err:
					default:
					}
					_ = conn.Close()
					return
				}
			}
		}
	}()

	for {
		_ = conn.SetReadDeadline(time.Now().Add(s.idleTimeout))
		opcode, payload, err := readMessage(reader)
		if err != nil {
			select {
			case werr := <-writeErr:
				return werr
			default:
			}
			if ctx.Err() != nil {
				return nil
			}
			return err
		}
		switch opcode {
		case opcodeText:
			s.handleFrame(payload)
		case opcodePing:
			if err := writeFrame(conn, opcodePong, payload); err != nil {
				return err
			}
		case opcodeClose:
			return errors.New("relay closed the event channel")
		}
	}
}

func (s *EventSocket) handleFrame(payload []byte) {
	var frame struct {
		Type  string `json:"type"`
		Topic string `json:"topic"`
		Repo  string `json:"repo"`
	}
	if err := json.Unmarshal(payload, &frame); err != nil || frame.Type != "event" {
		return
	}
	s.emit(Event{Topic: boundedText([]byte(frame.Topic), 40), Repo: boundedText([]byte(frame.Repo), 200)})
}

func (s *EventSocket) emit(event Event) {
	if s.Handler != nil {
		s.Handler(event)
	}
}

func (s *EventSocket) setConnected(connected bool) {
	if s.Connected != nil {
		s.Connected(connected)
	}
}

func (s *EventSocket) connect(ctx context.Context) (net.Conn, *bufio.Reader, error) {
	target, err := url.Parse(s.signedURL())
	if err != nil {
		return nil, nil, err
	}
	address := target.Host
	if target.Port() == "" {
		if target.Scheme == "wss" {
			address = net.JoinHostPort(target.Hostname(), "443")
		} else {
			address = net.JoinHostPort(target.Hostname(), "80")
		}
	}
	dialCtx, cancel := context.WithTimeout(ctx, 20*time.Second)
	defer cancel()
	var conn net.Conn
	switch {
	case s.dial != nil:
		conn, err = s.dial(dialCtx, "tcp", address)
	case target.Scheme == "wss":
		dialer := &tls.Dialer{Config: s.tlsConfig}
		conn, err = dialer.DialContext(dialCtx, "tcp", address)
	default:
		conn, err = (&net.Dialer{}).DialContext(dialCtx, "tcp", address)
	}
	if err != nil {
		return nil, nil, err
	}
	ok := false
	defer func() {
		if !ok {
			_ = conn.Close()
		}
	}()

	nonce := make([]byte, 16)
	if _, err := rand.Read(nonce); err != nil {
		return nil, nil, err
	}
	key := base64.StdEncoding.EncodeToString(nonce)
	requestURI := target.RequestURI()
	handshake := "GET " + requestURI + " HTTP/1.1\r\n" +
		"Host: " + target.Host + "\r\n" +
		"Upgrade: websocket\r\n" +
		"Connection: Upgrade\r\n" +
		"Sec-WebSocket-Key: " + key + "\r\n" +
		"Sec-WebSocket-Version: 13\r\n" +
		"User-Agent: ForkMesh-Mirror-Node\r\n\r\n"
	_ = conn.SetWriteDeadline(time.Now().Add(20 * time.Second))
	if _, err := io.WriteString(conn, handshake); err != nil {
		return nil, nil, err
	}
	_ = conn.SetReadDeadline(time.Now().Add(20 * time.Second))
	reader := bufio.NewReaderSize(conn, 4096)
	request, err := http.NewRequest(http.MethodGet, target.String(), nil)
	if err != nil {
		return nil, nil, err
	}
	response, err := http.ReadResponse(reader, request)
	if err != nil {
		return nil, nil, err
	}
	defer func() {
		_, _ = io.Copy(io.Discard, io.LimitReader(response.Body, eventHandshakeLimit))
		_ = response.Body.Close()
	}()
	if response.StatusCode != http.StatusSwitchingProtocols {
		body, _ := io.ReadAll(io.LimitReader(response.Body, 512))
		return nil, nil, fmt.Errorf("upgrade returned HTTP %d: %s",
			response.StatusCode, boundedText(body, 200))
	}
	digest := sha1.Sum([]byte(key + websocketGUID))
	if !strings.EqualFold(response.Header.Get("Upgrade"), "websocket") ||
		response.Header.Get("Sec-WebSocket-Accept") !=
			base64.StdEncoding.EncodeToString(digest[:]) {
		return nil, nil, errors.New("relay returned an invalid websocket handshake")
	}
	_ = conn.SetWriteDeadline(time.Time{})
	ok = true
	return conn, reader, nil
}

const (
	opcodeContinuation = 0x0
	opcodeText         = 0x1
	opcodeBinary       = 0x2
	opcodeClose        = 0x8
	opcodePing         = 0x9
	opcodePong         = 0xA
)

// readMessage joins continuation frames and rejects oversized messages.
func readMessage(reader *bufio.Reader) (int, []byte, error) {
	var (
		message []byte
		opcode  int
	)
	for {
		final, frameOpcode, payload, err := readFrame(reader)
		if err != nil {
			return 0, nil, err
		}
		if frameOpcode >= opcodeClose {
			return frameOpcode, payload, nil
		}
		if frameOpcode != opcodeContinuation {
			opcode = frameOpcode
			message = payload
		} else {
			message = append(message, payload...)
		}
		if len(message) > eventMaxFrameBytes {
			return 0, nil, errors.New("event frame too large")
		}
		if final {
			return opcode, message, nil
		}
	}
}

func readFrame(reader *bufio.Reader) (bool, int, []byte, error) {
	header := make([]byte, 2)
	if _, err := io.ReadFull(reader, header); err != nil {
		return false, 0, nil, err
	}
	final := header[0]&0x80 != 0
	opcode := int(header[0] & 0x0F)
	// RFC 6455 §5.1: a server must not mask the frames it sends.
	if header[1]&0x80 != 0 {
		return false, 0, nil, errors.New("relay sent a masked frame")
	}
	length := int64(header[1] & 0x7F)
	switch length {
	case 126:
		extended := make([]byte, 2)
		if _, err := io.ReadFull(reader, extended); err != nil {
			return false, 0, nil, err
		}
		length = int64(binary.BigEndian.Uint16(extended))
	case 127:
		extended := make([]byte, 8)
		if _, err := io.ReadFull(reader, extended); err != nil {
			return false, 0, nil, err
		}
		length = int64(binary.BigEndian.Uint64(extended) & 0x7FFFFFFFFFFFFFFF)
	}
	if length > eventMaxFrameBytes {
		return false, 0, nil, errors.New("event frame too large")
	}
	payload := make([]byte, length)
	if _, err := io.ReadFull(reader, payload); err != nil {
		return false, 0, nil, err
	}
	return final, opcode, payload, nil
}

// writeFrame emits one unfragmented client frame. Client frames are always
// masked (RFC 6455 §5.3); Cloudflare rejects an unmasked one outright.
func writeFrame(conn net.Conn, opcode int, payload []byte) error {
	if len(payload) > eventMaxFrameBytes {
		return errors.New("event frame too large")
	}
	header := []byte{byte(0x80 | opcode)}
	switch {
	case len(payload) < 126:
		header = append(header, byte(0x80|len(payload)))
	default:
		header = append(header, 0x80|126, 0, 0)
		binary.BigEndian.PutUint16(header[2:], uint16(len(payload)))
	}
	mask := make([]byte, 4)
	if _, err := rand.Read(mask); err != nil {
		return err
	}
	masked := make([]byte, len(payload))
	for i := range payload {
		masked[i] = payload[i] ^ mask[i%4]
	}
	_ = conn.SetWriteDeadline(time.Now().Add(20 * time.Second))
	defer conn.SetWriteDeadline(time.Time{})
	if _, err := conn.Write(append(append(header, mask...), masked...)); err != nil {
		return err
	}
	return nil
}
