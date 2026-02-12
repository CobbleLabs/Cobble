package commands

import (
	"crypto/sha256"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"strings"
	"sync"
	"time"

	"cobble-discord-bot/config"

	"github.com/bwmarrin/discordgo"
)

// Implementation notes:
// - Uses litecoinspace.org (mempool.space-compatible) endpoints.
// - Validates Litecoin *mainnet* addresses (Base58Check + Bech32/Bech32m).
// - Uses timeouts and small caches to be production-safe and reduce API load.

const (
	ltcExplorerBaseURL = "https://litecoinspace.org"
	ltcAPIBaseURL      = "https://litecoinspace.org/api"
)

var ltcHTTPClient = &http.Client{
	Timeout: 15 * time.Second,
	Transport: &http.Transport{
		Proxy: http.ProxyFromEnvironment,
		DialContext: (&net.Dialer{
			Timeout:   10 * time.Second,
			KeepAlive: 30 * time.Second,
		}).DialContext,
		TLSHandshakeTimeout:   10 * time.Second,
		ResponseHeaderTimeout: 10 * time.Second,
		IdleConnTimeout:       60 * time.Second,
		MaxIdleConns:          100,
		MaxIdleConnsPerHost:   20,
	},
}

type ltcTransaction struct {
	TxID   string `json:"txid"`
	Status struct {
		Confirmed   bool  `json:"confirmed"`
		BlockHeight int   `json:"block_height"`
		BlockTime   int64 `json:"block_time"`
	} `json:"status"`
	Vout []struct {
		ScriptPubKeyAddress string `json:"scriptpubkey_address"`
		Value               int64  `json:"value"`
	} `json:"vout"`
	Vin []struct {
		Prevout struct {
			ScriptPubKeyAddress string `json:"scriptpubkey_address"`
			Value               int64  `json:"value"`
		} `json:"prevout"`
	} `json:"vin"`
}

type ltcTipCache struct {
	mu     sync.Mutex
	height int
	at     time.Time
}

var ltcTip ltcTipCache

func CheckWalletCommand(s *discordgo.Session, i *discordgo.InteractionCreate) {
	// Admin-only (role-based) to match the rest of the bot.
	cfg := config.Get()
	hasAdmin := false
	if i.Member != nil {
		for _, role := range i.Member.Roles {
			if role == cfg.AdminRoleID {
				hasAdmin = true
				break
			}
		}
	}
	if !hasAdmin {
		_ = s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
			Type: discordgo.InteractionResponseChannelMessageWithSource,
			Data: &discordgo.InteractionResponseData{
				Content: "Only admins can use this command.",
				Flags:   discordgo.MessageFlagsEphemeral,
			},
		})
		return
	}

	// Defer so we can do network calls safely.
	if err := s.InteractionRespond(i.Interaction, &discordgo.InteractionResponse{
		Type: discordgo.InteractionResponseDeferredChannelMessageWithSource,
		Data: &discordgo.InteractionResponseData{Flags: discordgo.MessageFlagsEphemeral},
	}); err != nil {
		return
	}

	wallet, hours, err := parseCheckWalletOptions(i.ApplicationCommandData().Options)
	if err != nil {
		_, _ = s.InteractionResponseEdit(i.Interaction, &discordgo.WebhookEdit{
			Content: stringPtr("Error: " + err.Error()),
		})
		return
	}

	// This bot uses litecoinspace.org which is mainnet-only.
	if err := validateLitecoinMainnetAddress(wallet); err != nil {
		_, _ = s.InteractionResponseEdit(i.Interaction, &discordgo.WebhookEdit{
			Content: stringPtr("Error: " + err.Error()),
		})
		return
	}

	txs, err := getLitecoinTransactions(wallet)
	if err != nil {
		_, _ = s.InteractionResponseEdit(i.Interaction, &discordgo.WebhookEdit{
			Content: stringPtr(fmt.Sprintf("Error querying Litecoin API: %s", err.Error())),
		})
		return
	}

	tipHeight, _ := getCurrentLitecoinBlockHeightCached()

	cutoff := time.Now().Add(-time.Duration(hours) * time.Hour).Unix()
	recent := make([]ltcTransaction, 0, len(txs))
	for _, tx := range txs {
		// Pending mempool tx often have block_time = 0 on mempool-style APIs.
		if tx.Status.BlockTime == 0 || tx.Status.BlockTime >= cutoff {
			recent = append(recent, tx)
		}
	}

	if len(recent) == 0 {
		embed := &discordgo.MessageEmbed{
			Title:       "No recent transactions",
			Description: fmt.Sprintf("No transactions were found in the last %d hour(s) for:\n`%s`\n\nTotal known transactions: `%d`", hours, wallet, len(txs)),
			Color:       0xFAA61A, // orange
			Timestamp:   time.Now().Format(time.RFC3339),
			Footer:      &discordgo.MessageEmbedFooter{Text: "Increase the hours filter to search further back"},
		}
		_, _ = s.InteractionResponseEdit(i.Interaction, &discordgo.WebhookEdit{
			Embeds: &[]*discordgo.MessageEmbed{embed},
		})
		return
	}

	embed := &discordgo.MessageEmbed{
		Title:       "Litecoin wallet check",
		Description: fmt.Sprintf("Wallet: `%s`\nFilter: last `%d` hour(s)\nRecent transactions: `%d`\nTotal returned by API: `%d`", wallet, hours, len(recent), len(txs)),
		Color:       0x57F287, // green
		Timestamp:   time.Now().Format(time.RFC3339),
		Footer:      &discordgo.MessageEmbedFooter{Text: "Source: litecoinspace.org"},
	}

	limit := 10
	if len(recent) < limit {
		limit = len(recent)
	}

	for idx, tx := range recent[:limit] {
		status, confirmations := ltcTxStatus(tipHeight, &tx)

		received := ltcSumOutputsToAddress(&tx, wallet)
		spent := ltcSumInputsFromAddress(&tx, wallet)
		net := received - spent

		direction := "RECEIVED"
		amount := net
		if net < 0 {
			direction = "SENT"
			amount = -net
		}
		if net == 0 && (received > 0 || spent > 0) {
			direction = "SELF / NEUTRAL"
			amount = received
		}

		shortTxID := shortenTxID(tx.TxID)

		when := "moments ago"
		if tx.Status.BlockTime > 0 {
			when = humanizeDuration(time.Since(time.Unix(tx.Status.BlockTime, 0)))
		}

		field := fmt.Sprintf(
			"Direction: **%s**\nAmount: `%.8f LTC`\nStatus: %s\nConfirmations: `%d`\nTime: %s\nTxID: `%s`\nExplorer: %s/tx/%s",
			direction,
			satoshiToLTC(amount),
			status,
			confirmations,
			when,
			shortTxID,
			ltcExplorerBaseURL,
			tx.TxID,
		)

		embed.Fields = append(embed.Fields, &discordgo.MessageEmbedField{
			Name:   fmt.Sprintf("Transaction #%d", idx+1),
			Value:  field,
			Inline: false,
		})
	}

	if len(recent) > limit {
		embed.Fields = append(embed.Fields, &discordgo.MessageEmbedField{
			Name:   "Note",
			Value:  fmt.Sprintf("Showing the latest %d of %d recent transactions. Adjust `hours` to narrow or widen the search window.", limit, len(recent)),
			Inline: false,
		})
	}

	_, _ = s.InteractionResponseEdit(i.Interaction, &discordgo.WebhookEdit{
		Embeds: &[]*discordgo.MessageEmbed{embed},
	})
}

func parseCheckWalletOptions(options []*discordgo.ApplicationCommandInteractionDataOption) (wallet string, hours int64, _ error) {
	hours = 1
	for _, opt := range options {
		switch opt.Name {
		case "wallet":
			wallet = strings.TrimSpace(opt.StringValue())
		case "hours":
			hours = opt.IntValue()
		}
	}
	if wallet == "" {
		return "", 0, errors.New("you must provide a Litecoin wallet address")
	}
	if hours <= 0 {
		return "", 0, errors.New("hours must be >= 1")
	}
	if hours > 168 {
		return "", 0, errors.New("hours must be <= 168 (7 days)")
	}
	return wallet, hours, nil
}

func getLitecoinTransactions(address string) ([]ltcTransaction, error) {
	url := fmt.Sprintf("%s/address/%s/txs", ltcAPIBaseURL, address)
	req, err := http.NewRequest(http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Accept", "application/json")

	resp, err := ltcHTTPClient.Do(req)
	if err != nil {
		return nil, fmt.Errorf("connection error: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(io.LimitReader(resp.Body, 2048))
		if resp.StatusCode == http.StatusTooManyRequests {
			return nil, fmt.Errorf("rate limited (429). Try again later. %s", strings.TrimSpace(string(body)))
		}
		return nil, fmt.Errorf("unexpected status %d. %s", resp.StatusCode, strings.TrimSpace(string(body)))
	}

	body, err := io.ReadAll(io.LimitReader(resp.Body, 8<<20)) // 8 MiB safety cap
	if err != nil {
		return nil, fmt.Errorf("read error: %v", err)
	}

	var txs []ltcTransaction
	if err := json.Unmarshal(body, &txs); err != nil {
		return nil, fmt.Errorf("JSON parse error: %v", err)
	}
	return txs, nil
}

func getCurrentLitecoinBlockHeightCached() (int, error) {
	ltcTip.mu.Lock()
	if time.Since(ltcTip.at) < 30*time.Second && ltcTip.height > 0 {
		h := ltcTip.height
		ltcTip.mu.Unlock()
		return h, nil
	}
	ltcTip.mu.Unlock()

	h, err := getCurrentLitecoinBlockHeight()
	if err != nil {
		return 0, err
	}

	ltcTip.mu.Lock()
	ltcTip.height = h
	ltcTip.at = time.Now()
	ltcTip.mu.Unlock()

	return h, nil
}

func getCurrentLitecoinBlockHeight() (int, error) {
	url := ltcAPIBaseURL + "/blocks/tip/height"
	req, err := http.NewRequest(http.MethodGet, url, nil)
	if err != nil {
		return 0, err
	}
	resp, err := ltcHTTPClient.Do(req)
	if err != nil {
		return 0, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(io.LimitReader(resp.Body, 512))
		return 0, fmt.Errorf("unexpected status %d. %s", resp.StatusCode, strings.TrimSpace(string(body)))
	}

	body, err := io.ReadAll(io.LimitReader(resp.Body, 64))
	if err != nil {
		return 0, err
	}
	var height int
	if _, err := fmt.Sscanf(string(body), "%d", &height); err != nil {
		return 0, err
	}
	return height, nil
}

func ltcTxStatus(tipHeight int, tx *ltcTransaction) (status string, confirmations int) {
	if !tx.Status.Confirmed {
		return "PENDING", 0
	}
	if tipHeight <= 0 || tx.Status.BlockHeight <= 0 || tipHeight < tx.Status.BlockHeight {
		return "CONFIRMED", 0
	}
	c := tipHeight - tx.Status.BlockHeight + 1
	if c < 0 {
		c = 0
	}
	return "CONFIRMED", c
}

func ltcSumOutputsToAddress(tx *ltcTransaction, address string) int64 {
	var sum int64
	for _, o := range tx.Vout {
		if o.ScriptPubKeyAddress == address {
			sum += o.Value
		}
	}
	return sum
}

func ltcSumInputsFromAddress(tx *ltcTransaction, address string) int64 {
	var sum int64
	for _, in := range tx.Vin {
		if in.Prevout.ScriptPubKeyAddress == address {
			sum += in.Prevout.Value
		}
	}
	return sum
}

func satoshiToLTC(sats int64) float64 {
	return float64(sats) / 100000000.0
}

func shortenTxID(txid string) string {
	txid = strings.TrimSpace(txid)
	if len(txid) <= 16 {
		return txid
	}
	return txid[:8] + "..." + txid[len(txid)-8:]
}

func humanizeDuration(d time.Duration) string {
	if d < time.Minute {
		return "less than 1 minute ago"
	}
	if d < time.Hour {
		m := int(d.Minutes())
		if m == 1 {
			return "1 minute ago"
		}
		return fmt.Sprintf("%d minutes ago", m)
	}
	if d < 24*time.Hour {
		h := int(d.Hours())
		if h == 1 {
			return "1 hour ago"
		}
		return fmt.Sprintf("%d hours ago", h)
	}
	days := int(d.Hours() / 24)
	if days == 1 {
		return "1 day ago"
	}
	return fmt.Sprintf("%d days ago", days)
}

// ---- Address validation (mainnet only) ----

func validateLitecoinMainnetAddress(addr string) error {
	addr = strings.TrimSpace(addr)
	if addr == "" {
		return errors.New("wallet address is empty")
	}

	// Bech32/Bech32m (SegWit): ltc1...
	if strings.HasPrefix(strings.ToLower(addr), "ltc1") {
		if err := validateBech32Segwit("ltc", addr); err != nil {
			return fmt.Errorf("invalid bech32 address: %w", err)
		}
		return nil
	}

	// Base58Check (legacy): typically L... (P2PKH), M... (P2SH), sometimes 3... (old P2SH).
	if err := validateBase58CheckLitecoin(addr); err != nil {
		return fmt.Errorf("invalid base58 address: %w", err)
	}
	return nil
}

func validateBase58CheckLitecoin(addr string) error {
	decoded, err := base58Decode(addr)
	if err != nil {
		return err
	}
	if len(decoded) < 5 {
		return errors.New("too short")
	}
	payload := decoded[:len(decoded)-4]
	checksum := decoded[len(decoded)-4:]

	sum := doubleSHA256(payload)[:4]
	if !bytesEqual(sum, checksum) {
		return errors.New("checksum mismatch")
	}

	// Litecoin mainnet version bytes:
	// - 0x30 (48): P2PKH (L...)
	// - 0x32 (50): P2SH  (M...)
	// Some older Litecoin P2SH addresses used 0x05 (3...); keep accepted to avoid breaking old wallets.
	ver := payload[0]
	switch ver {
	case 0x30, 0x32, 0x05:
		return nil
	default:
		return fmt.Errorf("unexpected version byte 0x%02x (this command supports Litecoin mainnet only)", ver)
	}
}

func doubleSHA256(b []byte) []byte {
	h := sha256.Sum256(b)
	h2 := sha256.Sum256(h[:])
	return h2[:]
}

func bytesEqual(a, b []byte) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

const base58Alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"

func base58Decode(s string) ([]byte, error) {
	s = strings.TrimSpace(s)
	if s == "" {
		return nil, errors.New("empty string")
	}

	// Convert base58 string to big-endian bytes without importing big.Int (small, fast).
	// Adapted from standard base58 decode logic.
	var out []byte
	for i := 0; i < len(s); i++ {
		ch := s[i]
		idx := strings.IndexByte(base58Alphabet, ch)
		if idx < 0 {
			return nil, fmt.Errorf("invalid base58 character %q", ch)
		}

		carry := idx
		for j := len(out) - 1; j >= 0; j-- {
			carry += int(out[j]) * 58
			out[j] = byte(carry & 0xff)
			carry >>= 8
		}
		for carry > 0 {
			out = append([]byte{byte(carry & 0xff)}, out...)
			carry >>= 8
		}
	}

	// Preserve leading zeros encoded as leading '1's.
	leading := 0
	for leading < len(s) && s[leading] == '1' {
		leading++
	}
	if leading > 0 {
		prefix := make([]byte, leading)
		out = append(prefix, out...)
	}
	return out, nil
}

// ---- Bech32/Bech32m (BIP-173/BIP-350) ----

const bech32Charset = "qpzry9x8gf2tvdw0s3jn54khce6mua7l"

func validateBech32Segwit(expectedHRP, addr string) error {
	hrp, data, spec, err := bech32Decode(addr)
	if err != nil {
		return err
	}
	if hrp != expectedHRP {
		return fmt.Errorf("unexpected HRP %q", hrp)
	}
	if len(data) < 1 {
		return errors.New("missing witness version")
	}

	witVer := data[0]
	if witVer > 16 {
		return fmt.Errorf("invalid witness version %d", witVer)
	}

	// Convert 5-bit groups to 8-bit program bytes.
	prog, err := convertBits(data[1:], 5, 8, false)
	if err != nil {
		return err
	}
	if l := len(prog); l < 2 || l > 40 {
		return fmt.Errorf("invalid witness program length %d", l)
	}
	if witVer == 0 && len(prog) != 20 && len(prog) != 32 {
		return fmt.Errorf("invalid v0 witness program length %d", len(prog))
	}

	// Enforce checksum variant for segwit:
	// - v0 uses bech32
	// - v1+ uses bech32m
	if witVer == 0 && spec != bech32SpecBech32 {
		return errors.New("v0 segwit must use bech32 checksum")
	}
	if witVer != 0 && spec != bech32SpecBech32m {
		return errors.New("v1+ segwit must use bech32m checksum")
	}

	return nil
}

type bech32Spec int

const (
	bech32SpecBech32 bech32Spec = iota
	bech32SpecBech32m
)

func bech32Decode(addr string) (hrp string, data []byte, spec bech32Spec, _ error) {
	if addr == "" {
		return "", nil, 0, errors.New("empty address")
	}
	if strings.ToLower(addr) != addr && strings.ToUpper(addr) != addr {
		return "", nil, 0, errors.New("mixed case")
	}
	a := strings.ToLower(addr)
	pos := strings.LastIndexByte(a, '1')
	if pos < 1 || pos+7 > len(a) { // need hrp + '1' + 6 checksum chars at least
		return "", nil, 0, errors.New("invalid separator position")
	}
	hrp = a[:pos]
	dataPart := a[pos+1:]
	data = make([]byte, 0, len(dataPart))
	for i := 0; i < len(dataPart); i++ {
		ch := dataPart[i]
		idx := strings.IndexByte(bech32Charset, ch)
		if idx < 0 {
			return "", nil, 0, fmt.Errorf("invalid bech32 character %q", ch)
		}
		data = append(data, byte(idx))
	}

	p := bech32Polymod(bech32HRPExpand(hrp), data)
	switch p {
	case 1:
		spec = bech32SpecBech32
	case 0x2bc830a3:
		spec = bech32SpecBech32m
	default:
		return "", nil, 0, errors.New("checksum mismatch")
	}

	// Strip checksum (last 6 values).
	if len(data) < 6 {
		return "", nil, 0, errors.New("data too short")
	}
	return hrp, data[:len(data)-6], spec, nil
}

func bech32HRPExpand(hrp string) []byte {
	out := make([]byte, 0, len(hrp)*2+1)
	for i := 0; i < len(hrp); i++ {
		out = append(out, hrp[i]>>5)
	}
	out = append(out, 0)
	for i := 0; i < len(hrp); i++ {
		out = append(out, hrp[i]&31)
	}
	return out
}

func bech32Polymod(hrpExpand []byte, data []byte) uint32 {
	values := make([]byte, 0, len(hrpExpand)+len(data))
	values = append(values, hrpExpand...)
	values = append(values, data...)

	var chk uint32 = 1
	for i := 0; i < len(values); i++ {
		top := chk >> 25
		chk = (chk&0x1ffffff)<<5 ^ uint32(values[i])
		if (top & 1) != 0 {
			chk ^= 0x3b6a57b2
		}
		if (top & 2) != 0 {
			chk ^= 0x26508e6d
		}
		if (top & 4) != 0 {
			chk ^= 0x1ea119fa
		}
		if (top & 8) != 0 {
			chk ^= 0x3d4233dd
		}
		if (top & 16) != 0 {
			chk ^= 0x2a1462b3
		}
	}
	return chk
}

func convertBits(data []byte, fromBits, toBits uint, pad bool) ([]byte, error) {
	var acc uint
	var bits uint
	var ret []byte
	maxv := uint((1 << toBits) - 1)
	maxAcc := uint((1 << (fromBits + toBits - 1)) - 1)

	for i := 0; i < len(data); i++ {
		value := uint(data[i])
		if value>>fromBits != 0 {
			return nil, errors.New("invalid data range")
		}
		acc = ((acc << fromBits) | value) & maxAcc
		bits += fromBits
		for bits >= toBits {
			bits -= toBits
			ret = append(ret, byte((acc>>bits)&maxv))
		}
	}

	if pad {
		if bits > 0 {
			ret = append(ret, byte((acc<<(toBits-bits))&maxv))
		}
	} else {
		if bits >= fromBits {
			return nil, errors.New("illegal zero padding")
		}
		if ((acc << (toBits - bits)) & maxv) != 0 {
			return nil, errors.New("non-zero padding")
		}
	}
	return ret, nil
}
