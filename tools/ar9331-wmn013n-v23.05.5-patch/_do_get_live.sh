do_get() {
	ok_num "$id" || jerr 400 "bad id"
	row=$(sq "SELECT dtype,path,fmt,bytes,rate FROM records WHERE id=$id;")
	[ -n "$row" ] || jerr 404 "no such record"
	dtyp=${row%%|*}; rest=${row#*|}
	path=${rest%%|*}; rest=${rest#*|}
	fmt=${rest%%|*}; rest=${rest#*|}
	bytes=${rest%%|*}; rate=${rest#*|}

	if [ "$dtyp" != "file" ]; then
		o=$(sqlite3 -batch -json "$DB" "SELECT id,devid,channel,ts,client_ts,dtype,val,txt,data FROM records WHERE id=$id;")
		[ -n "$o" ] || o='[]'
		printf 'Status: 200 OK\r\nContent-Type: application/json\r\n\r\n{"ok":1,"row":%s}\n' "$o"
		return
	fi

	f="$ROOT/$path"
	[ -f "$f" ] || jerr 404 "file missing"
	[ -n "$rate" ] || rate=8000

	tag=0
	[ "$raw" != 1 ] && case "$fmt" in
		alaw) tag=6;;
		ulaw) tag=7;;
		pcm)  tag=1;;
	esac

	if [ "$tag" != 0 ]; then
		hdr=$(wav_hdr "$bytes" "$rate" "$tag")
		printf 'Status: 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: %s\r\nContent-Disposition: inline; filename="%s.wav"\r\n\r\n' \
			"$((bytes+44))" "$id"
		printf "$hdr"
		cat "$f"
		return
	fi

	# LIVEFIX_HUBFS_REDIRECT: large files via /www/hubfs (avoid CGI ~60s cut)
	# Location 必须是绝对 URL: 手表 httpcli 无法解析相对路径 /hubfs/...
	if [ "$bytes" -gt 262144 ] 2>/dev/null; then
		host=${HTTP_HOST:-$SERVER_ADDR}
		host=${host%%:*}
		[ -n "$host" ] || host=192.168.5.28
		printf 'Status: 302 Found\r\nLocation: http://%s/hubfs/%s\r\nCache-Control: public, max-age=3600\r\n\r\n' "$host" "$path"
		return
	fi

	ct="application/octet-stream"
	case "$fmt" in
		mp3)  ct="audio/mpeg";;
		m4a)  ct="audio/mp4";;
		aac)  ct="audio/aac";;
		flac) ct="audio/flac";;
		ogg)  ct="audio/ogg";;
		opus) ct="audio/ogg";;
		wav)  ct="audio/wav";;
		amr)  ct="audio/amr";;
		ape)  ct="audio/x-ape";;
		wma)  ct="audio/x-ms-wma";;
	esac
	ext=$fmt; [ -n "$ext" ] || ext=bin
	printf 'Status: 200 OK\r\nContent-Type: %s\r\nContent-Length: %s\r\nContent-Disposition: attachment; filename="%s.%s"\r\n\r\n' \
		"$ct" "$bytes" "$id" "$ext"
	cat "$f"
}
