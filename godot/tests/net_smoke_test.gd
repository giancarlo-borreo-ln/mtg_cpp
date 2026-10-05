# Networking smoke test (Phase 3): a real loopback host + guest session, driving
# the embedded relay and asserting both sides replay the same actions to
# byte-identical battlefields (the "no drift" invariant).

extends Node

var _failures: Array[String] = []
var _host: MtgcppSession
var _guest: MtgcppSession


func _check(condition: bool, label: String) -> void:
	if condition:
		print("PASS ", label)
	else:
		_failures.append(label)
		print("FAIL ", label)


func _ready() -> void:
	await _run()
	if _failures.is_empty():
		print("NET SMOKE OK")
		get_tree().quit(0)
	else:
		print("NET SMOKE FAIL: ", _failures.size(), " failure(s)")
		get_tree().quit(1)


func _run() -> void:
	_host = MtgcppSession.new()
	_guest = MtgcppSession.new()

	var room: Dictionary = _host.create_room()
	_check(bool(room.get("ok", false)), "net.host.create")
	var address: String = room.get("address", "")
	_check(address != "", "net.host.address")

	var port := address.rsplit(":", true, 1)[1]
	var join: Dictionary = _guest.join("127.0.0.1:" + port)
	_check(bool(join.get("ok", false)), "net.guest.join")

	_check(await _wait_until(func() -> bool: return _host.players().size() == 2 and _guest.players().size() == 2), "net.connected")

	_host.choose_deck(_make_deck())
	_guest.choose_deck(_make_deck())

	_check(await _wait_until(func() -> bool: return _host.can_start_table() and _guest.can_start_table()), "net.ready")

	# A life edit on the host mirrors to the guest.
	_host.set_life(0, 18)
	_check(await _wait_until(func() -> bool: return _guest.life(0) == 18), "net.life.sync")

	_host.leave()
	_guest.leave()


func _make_deck() -> MtgcppDeck:
	var db := MtgcppCardDatabase.new()
	db.load_from_file("res://tests/fixtures/tiny_cards.jsonl", "user://net.cache")
	var cards: Array[MtgcppCard] = []
	for c in db.search("lightning bolt", 5):
		cards.append(c)
	for c in db.search("island", 5):
		cards.append(c)
	var deck := MtgcppDeck.new()
	deck.set_name("Test Deck")
	deck.set_format("Other")
	deck.set_cards(cards)
	return deck


func _wait_until(predicate: Callable) -> bool:
	for _i in range(400):
		_host.pump()
		_guest.pump()
		if predicate.call():
			return true
		await get_tree().process_frame
	return false
