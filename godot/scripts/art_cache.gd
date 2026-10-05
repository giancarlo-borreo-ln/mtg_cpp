# Runtime card-art cache (Phase 4), autoloaded as `ArtCache`.
#
# Scryfall card *data* is CC0 but card *art* is not: art is downloaded on demand
# and cached locally for personal play, never redistributed (same policy as the
# desktop app). This is the Godot-native equivalent of `src/ui/art_cache.cpp`:
#
#   * HTTPRequest downloads images asynchronously with a small concurrency cap
#     (never blocking the main thread);
#   * decoded images are downscaled to a fixed card size, persisted as PNG under
#     `user://art_cache/<id>.png`, and kept in a bounded LRU texture map so the
#     memory footprint stays flat no matter how many cards are browsed;
#   * cards with no URL / still downloading / offline keep their procedural face
#     (the CardView fallback).
#
# Tests never touch the network: the cache is disabled headlessly and a
# `put_texture` seam injects a decoded texture directly.

extends Node

signal art_ready(key: String)

const CACHE_DIR := "user://art_cache"
const TARGET_SIZE := Vector2i(244, 340)   # MTG card aspect, ~0.72
const MAX_CONCURRENT := 4
const MAX_TEXTURES := 256
const TIMEOUT := 20.0

var enabled := true

var _textures := {}                # key -> Texture2D
var _lru: Array[String] = []       # most-recently-used last
var _inflight := {}                # key -> true
var _queue: Array = []             # [{ key, url }]
var _active := 0
var _pool: Array[HTTPRequest] = []


func _ready() -> void:
	# Headless runs (tests/CI) must stay offline and deterministic.
	enabled = DisplayServer.get_name() != "headless"
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(CACHE_DIR))


func set_enabled(value: bool) -> void:
	enabled = value


# The cached texture for `key`, or null. A miss with a non-empty `url` enqueues a
# download and emits `art_ready(key)` once it lands.
func texture_for(key: String, url: String) -> Texture2D:
	if not enabled or key.is_empty():
		return null
	if _textures.has(key):
		_touch(key)
		return _textures[key]

	var texture := _load_disk(key)
	if texture != null:
		_store(key, texture)
		return texture

	if not url.is_empty() and not _inflight.has(key):
		_inflight[key] = true
		_queue.append({"key": key, "url": url})
		_pump()
	return null


# Test seam: inject an already-decoded texture and notify listeners.
func put_texture(key: String, texture: Texture2D) -> void:
	if key.is_empty():
		return
	_store(key, texture)
	_inflight.erase(key)
	art_ready.emit(key)


func cached_count() -> int:
	return _textures.size()


func clear() -> void:
	_textures.clear()
	_lru.clear()
	_inflight.clear()
	_queue.clear()


# --- download queue ---------------------------------------------------------

func _pump() -> void:
	while _active < MAX_CONCURRENT and not _queue.is_empty():
		var job: Dictionary = _queue.pop_front()
		_start(str(job["key"]), str(job["url"]))


func _start(key: String, url: String) -> void:
	var http := _acquire_http()
	_active += 1
	http.request_completed.connect(
		func(_result: int, code: int, _headers: PackedStringArray, body: PackedByteArray) -> void:
			_on_done(http, key, code, body),
		Object.CONNECT_ONE_SHOT)
	if http.request(url, ["User-Agent: mtg_cpp-godot/1.0"]) != OK:
		_release_http(http)
		_active -= 1
		_inflight.erase(key)
		_pump()


func _on_done(http: HTTPRequest, key: String, code: int, body: PackedByteArray) -> void:
	_release_http(http)
	_active -= 1
	if code == 200 and body.size() > 0:
		var image := _decode(body)
		if image != null:
			image.resize(TARGET_SIZE.x, TARGET_SIZE.y, Image.INTERPOLATE_LANCZOS)
			_save_disk(key, image)
			_store(key, ImageTexture.create_from_image(image))
			art_ready.emit(key)
			_pump()
			return
	_inflight.erase(key)  # failed: allow a later retry
	_pump()


func _acquire_http() -> HTTPRequest:
	while not _pool.is_empty():
		var pooled: HTTPRequest = _pool.pop_back()
		if is_instance_valid(pooled):
			return pooled
	var http := HTTPRequest.new()
	http.timeout = TIMEOUT
	add_child(http)
	return http


func _release_http(http: HTTPRequest) -> void:
	if is_instance_valid(http):
		http.cancel_request()
		_pool.append(http)


# Scryfall serves PNG (preferred) or JPEG; decode whichever arrives.
func _decode(body: PackedByteArray) -> Image:
	var image := Image.new()
	if image.load_png_from_buffer(body) == OK:
		return image
	if image.load_jpg_from_buffer(body) == OK:
		return image
	if image.load_webp_from_buffer(body) == OK:
		return image
	return null


# --- disk + memory cache ----------------------------------------------------

func _disk_path(key: String) -> String:
	return CACHE_DIR.path_join(_sanitize(key) + ".png")


# A scryfall id is UUID-like, but sanitize defensively so an untrusted id can
# never escape the cache dir.
func _sanitize(key: String) -> String:
	var out := ""
	for i in mini(key.length(), 80):
		var c := key[i]
		out += c if (c.is_valid_identifier() or c.is_valid_int() or c == "-" or c == "_") else "_"
	return out if not out.is_empty() else key.sha1_text()


func _load_disk(key: String) -> Texture2D:
	var path := _disk_path(key)
	if not FileAccess.file_exists(path):
		return null
	var image := Image.load_from_file(path)
	if image == null or image.is_empty():
		return null
	if image.get_width() != TARGET_SIZE.x or image.get_height() != TARGET_SIZE.y:
		image.resize(TARGET_SIZE.x, TARGET_SIZE.y, Image.INTERPOLATE_LANCZOS)
	return ImageTexture.create_from_image(image)


func _save_disk(key: String, image: Image) -> void:
	# A failed cache write (read-only dir, disk full) is never fatal.
	image.save_png(_disk_path(key))


func _store(key: String, texture: Texture2D) -> void:
	_textures[key] = texture
	_touch(key)
	while _lru.size() > MAX_TEXTURES:
		var evicted: String = _lru.pop_front()
		_textures.erase(evicted)


func _touch(key: String) -> void:
	_lru.erase(key)
	_lru.append(key)
