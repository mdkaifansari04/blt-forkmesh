package mirrornode

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"testing"
)

func TestRegisterNodeAccountReservesAndFinalizesLinkCode(t *testing.T) {
	identity, err := GenerateIdentity(filepath.Join(t.TempDir(), "identity.pem"))
	if err != nil {
		t.Fatal(err)
	}
	var leaves []string
	server := httptest.NewServer(http.HandlerFunc(func(writer http.ResponseWriter, request *http.Request) {
		leaves = append(leaves, request.URL.Path)
		var body map[string]any
		if err := json.NewDecoder(request.Body).Decode(&body); err != nil {
			t.Error(err)
		}
		if body["nodeName"] != "mirror17" || body["pubkey"] != identity.PublicKey() {
			t.Errorf("unexpected request: %+v", body)
		}
		if request.URL.Path == "/api/accounts/finalize" {
			if body["linkCode"] != "123456" {
				t.Errorf("missing link code: %+v", body)
			}
			writer.WriteHeader(http.StatusCreated)
			return
		}
		writer.WriteHeader(http.StatusCreated)
	}))
	defer server.Close()
	if err := RegisterNodeAccount(context.Background(), server.URL+"/api/accounts", "mirror17", "123456", identity); err != nil {
		t.Fatal(err)
	}
	if len(leaves) != 2 || leaves[0] != "/api/accounts/reserve" || leaves[1] != "/api/accounts/finalize" {
		t.Fatalf("unexpected calls: %+v", leaves)
	}
}

func TestRegisterNodeAccountRejectsInvalidLinkCode(t *testing.T) {
	identity, err := GenerateIdentity(filepath.Join(t.TempDir(), "identity.pem"))
	if err != nil {
		t.Fatal(err)
	}
	if err := RegisterNodeAccount(context.Background(), "https://example.test/api/accounts", "mirror17", "12x456", identity); err == nil {
		t.Fatal("invalid link code accepted")
	}
}
