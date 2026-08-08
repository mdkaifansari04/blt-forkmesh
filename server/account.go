package mirrornode

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"
)

// RegisterNodeAccount reserves a key-bound account for a provisioning desktop.
func RegisterNodeAccount(ctx context.Context, accountsURL, node, linkCode string, identity *Identity) error {
	base, err := url.Parse(strings.TrimRight(accountsURL, "/"))
	localHTTP := base.Scheme == "http" &&
		(base.Hostname() == "127.0.0.1" || base.Hostname() == "localhost" || base.Hostname() == "::1")
	if err != nil || (base.Scheme != "https" && !localHTTP) || base.Host == "" || !nodePattern.MatchString(node) {
		return errors.New("accounts URL or node name is invalid")
	}
	if len(linkCode) != 6 {
		return errors.New("link code must contain six digits")
	}
	for _, character := range linkCode {
		if character < '0' || character > '9' {
			return errors.New("link code must contain six digits")
		}
	}
	timestamp := strconv.FormatInt(time.Now().UnixMilli(), 10)
	reserveCanonical := []byte("forkmesh-reserve-v1\n" + node + "\n" + timestamp)
	reserve := map[string]any{"nodeName": node, "pubkey": identity.PublicKey(), "ts": timestamp, "sig": identity.Sign(reserveCanonical)}
	status, _, err := postAccountJSON(ctx, base, "reserve", reserve)
	if err != nil {
		return err
	}
	if status != http.StatusCreated && status != http.StatusConflict {
		return fmt.Errorf("account reserve returned HTTP %d", status)
	}
	finalTimestamp := strconv.FormatInt(time.Now().UnixMilli(), 10)
	finalCanonical := []byte("forkmesh-finalize-v1\n" + node + "\n\n" + finalTimestamp)
	finalize := map[string]any{
		"nodeName": node, "email": "", "password": "", "pubkey": identity.PublicKey(),
		"ts": finalTimestamp, "sig": identity.Sign(finalCanonical), "linkCode": linkCode,
	}
	status, body, err := postAccountJSON(ctx, base, "finalize", finalize)
	if err != nil {
		return err
	}
	if status == http.StatusCreated {
		return nil
	}
	return fmt.Errorf("account finalize returned HTTP %d: %s", status, boundedText(body, 200))
}

func postAccountJSON(ctx context.Context, base *url.URL, leaf string, value any) (int, []byte, error) {
	body, err := json.Marshal(value)
	if err != nil {
		return 0, nil, err
	}
	target := *base
	target.Path = strings.TrimRight(base.Path, "/") + "/" + leaf
	request, err := http.NewRequestWithContext(ctx, http.MethodPost, target.String(), bytes.NewReader(body))
	if err != nil {
		return 0, nil, err
	}
	request.Header.Set("Content-Type", "application/json")
	client := &http.Client{Timeout: 20 * time.Second}
	response, err := client.Do(request)
	if err != nil {
		return 0, nil, err
	}
	defer response.Body.Close()
	responseBody, _ := io.ReadAll(io.LimitReader(response.Body, 4096))
	return response.StatusCode, responseBody, nil
}
