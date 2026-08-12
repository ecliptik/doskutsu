#!/bin/sh
#
# qa-analyze.sh -- the ONLY sanctioned producer of doskutsu QA campaign numbers.
#
# Every number in the QA matrix used to come from hand-written awk, re-derived
# once per round. That process twice published an estimated median as if it had
# been measured. This script exists so that no campaign number is ever typed by
# hand again: point it at a round directory, paste its output.
#
# Usage:
#   tools/qa-analyze.sh [options] <round-dir> [round-dir ...]
#
# A round directory holds cell logs as <TAG>.LOG plus <TAG>SDL.LOG. If the
# directory holds no *.LOG but has a logs/ subdirectory, that is used instead.
#
# Options:
#   -v            verbose -- print the full hardware-witness block per cell
#   -f FORMAT     table (default), tsv, or csv (ROUND1-CELLS.csv schema)
#   -h            this help
#
# Exit status:
#   0  all gates passed
#   1  one or more gates FAILED (see the GATES section of the output)
#   2  usage error
#
# ---------------------------------------------------------------------------
# THE METRIC
#
# Use the [fps-true] line. It is clock-independent (flips divided by seconds
# since the first flip) and therefore survives the corrupted uclock timebase.
# Never use fps_mean on its own: it conflates two independent quantities.
#
#     per-loop fps = flips / 102       102 s = the QA.TAS reel's game time
#     overhead_s   = render_s - 102    wall seconds spent NOT looping
#
# The reel is a fixed ~5100 logic ticks, so flips are near-constant across
# cells and render_s is what actually moves. A backend that looks slow on
# fps_mean is usually paying load stalls (GF1 DRAM uploads, PCM cache reads),
# not synthesis cost -- which is a completely different problem to fix.
#
# per-loop fps divides by the reel's game time, so pre-reel title flips fall
# inside the measurement window. Differences under 0.5 fps are inside that
# caveat. This tool groups cells into BANDs of 0.5 fps and you must not report
# a within-band difference as a difference.
#
# ---------------------------------------------------------------------------
# RUNMANIFEST SCHEMA VERSIONS
#
# The manifest is versioned and one field CHANGES MEANING across the bump, so
# every read of it is gated on schema_version. Absent or unrecognised is v1.
#
#   v1  binary_sha12 is NOT a binary hash. It has been described both as the
#       Organya cache key and as a source-diff fingerprint; this tool does
#       not adjudicate and reports it as "provenance_key". Either way the
#       real binary sha is not recorded anywhere in a v1 log, so this tool
#       reports NO binary sha for a v1 round -- quoting this value under the
#       name "binary sha" is the exact defect the v2 bump exists to fix.
#   v2  binary_sha12 carries the real binary sha; the cache key moves to its
#       own organya_cache_key field. Also adds organya_cache_key, per_loop_fps,
#       overhead_s, populated fps_p50/fps_p95, a CPU/speed-class witness and
#       per-stage fps lines -- all additive. Unknown keys are ignored, never
#       fatal, so a future v3 still parses.
#
# v2 emits the literal INVALID_PUMP in inter_flip fields on gus/adlib cells.
# The pump-detection gate below is kept regardless, because it is the only
# thing protecting the v1 corpus -- which is all of the banked data.
# ---------------------------------------------------------------------------

set -u

PROG="qa-analyze"
REEL_S=102
REEL_TICKS=5140
ROUTE_FULL="72 20 11 17 11 15 11 19 11 14 11"
BAND_FPS=0.5

VERBOSE=0
FORMAT=table
FAILED=0

usage() {
	sed -n '3,32p' "$0" | sed 's/^# \{0,1\}//'
	exit "${1:-2}"
}

die() {
	echo "$PROG: $*" >&2
	exit 2
}

while [ $# -gt 0 ]; do
	case "$1" in
	-v) VERBOSE=1; shift ;;
	-f) [ $# -ge 2 ] || die "-f needs an argument"; FORMAT="$2"; shift 2 ;;
	-h|--help) usage 0 ;;
	--) shift; break ;;
	-*) die "unknown option '$1' (try -h)" ;;
	*) break ;;
	esac
done

[ $# -ge 1 ] || usage 2
case "$FORMAT" in
table|tsv|csv) ;;
*) die "unknown format '$FORMAT' (want table, tsv or csv)" ;;
esac

# File descriptor 3 carries the human report. For the machine-readable
# formats it goes to stderr so that stdout is pure data and can be piped or
# redirected straight into a file without gate text landing in the dataset.
if [ "$FORMAT" = table ]; then exec 3>&1; else exec 3>&2; fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/qa-analyze.XXXXXX") || die "cannot mktemp"
trap 'rm -rf "$TMP"' EXIT INT TERM

# ---------------------------------------------------------------------------
# cell_scan MAIN_LOG SDL_LOG
#
# Emits one "key=value" line per extracted field. All logs are DOS CRLF, so
# every line is stripped of its trailing CR first -- without this every field
# silently carries a \r and every string comparison in this script fails.
# ---------------------------------------------------------------------------
cell_scan() {
	awk '
	{ sub(/\r$/, "") }

	# --- the metric -----------------------------------------------------
	/\[fps-true\]/ && !seen_fps {
		if (match($0, /flips=[0-9]+/))
			print "flips=" substr($0, RSTART + 6, RLENGTH - 6)
		if (match($0, /render_s=[0-9]+/))
			print "render_s=" substr($0, RSTART + 9, RLENGTH - 9)
		if (match($0, /fps_mean=[0-9.]+/))
			print "fps_mean=" substr($0, RSTART + 9, RLENGTH - 9)
		seen_fps = 1
		next
	}

	# --- backend --------------------------------------------------------
	/audio backend: / && !seen_be {
		if (match($0, /audio backend: [a-z0-9]+/)) {
			print "backend=" substr($0, RSTART + 15, RLENGTH - 15)
			seen_be = 1
		}
		next
	}

	# --- pump markers ---------------------------------------------------
	# ANCHOR CAREFULLY. The [fps-true] line itself contains the phrase
	# "PIT/IRQ-0 music pump" in its own caveat text, so a loose /PIT.IRQ-0/
	# match flags every cell in the corpus, pumped or not. Both patterns
	# below are checked against the exact banner wording.
	# These two deliberately do NOT "next": the GUS "backend ready" banner
	# carries both the pump phrase and the GF1 voice/DRAM witnesses on one
	# line, so consuming the line here would silently drop the witnesses.
	/OPL timer pump STARTED/ { print "pump_opl=1" }
	/PIT\/IRQ-0 pump/        { print "pump_pit=1" }

	# --- route ----------------------------------------------------------
	/>> Entering stage / {
		if (match($0, /Entering stage [0-9]+/))
			route = route (route == "" ? "" : " ") \
				substr($0, RSTART + 15, RLENGTH - 15)
		next
	}

	# --- provenance -----------------------------------------------------
	# RUNMANIFEST is versioned and binary_sha12 CHANGES MEANING between
	# versions, so nothing here may be read without first knowing the
	# schema. See resolve-by-schema in the shell below.
	/^schema_version=/      { print "schema="     substr($0, 16); next }
	/^binary_sha12=/        { print "sha12_raw="  substr($0, 14); next }
	/^binary_sha12_src=/    { print "sha12_src="  substr($0, 18); next }
	/^organya_cache_key=/   { print "org_key_v2=" substr($0, 19); next }
	/^wave_tag=/      { print "wave_tag="   substr($0, 10); next }
	/^exit_code=/     { print "exit_code="  substr($0, 11); next }
	/^duration_s=/    { print "duration_s=" substr($0, 12); next }
	/^env_block_sha=/ { print "env_sha="    substr($0, 15); next }

	/header_seed=/ && !seen_seed {
		if (match($0, /header_seed=[0-9]+/)) {
			print "seed=" substr($0, RSTART + 12, RLENGTH - 12)
			seen_seed = 1
		}
		next
	}
	/end-of-replay auto-exit at tick / {
		if (match($0, /at tick [0-9]+/))
			print "end_tick=" substr($0, RSTART + 8, RLENGTH - 8)
		next
	}
	/\[pxt-cache\] init\(\):/ {
		if (match($0, /init\(\): [A-Z]+/))
			print "pxtcache=" substr($0, RSTART + 8, RLENGTH - 8)
		next
	}

	# --- hardware witnesses ---------------------------------------------
	# UNIVBE rewrites the VBE OEM strings, so the card cannot be named from
	# them. VRAM size is the only usable proxy -- which is exactly what
	# patches/SDL/0019 keys on. Any card name this tool prints is INFERRED.
	/DOSVESA-CTRL: VBE/ {
		if (match($0, /total_vram=[0-9]+_KB/))
			print "vram_kb=" substr($0, RSTART + 11, RLENGTH - 14)
		next
	}
	/cirrus-lfb-aperture-bug: detected/     { print "lfb_guard=FIRED"; next }
	/cirrus-lfb-aperture-bug: NOT detected/ { print "lfb_guard=no";    next }
	/WAVE16-MODESET:/ && !seen_mode {
		if (match($0, /mode=0x[0-9A-Fa-f]+/))
			print "mode=" substr($0, RSTART + 5, RLENGTH - 5)
		if (match($0, /mode=0x[0-9A-Fa-f]+ [0-9]+x[0-9]+/)) {
			s = substr($0, RSTART, RLENGTH)
			sub(/^mode=0x[0-9A-Fa-f]+ /, "", s)
			print "res=" s
		}
		if (match($0, /fb_base=[A-Za-z]+/))
			print "fb_base=" substr($0, RSTART + 8, RLENGTH - 8)
		if (match($0, /actual_bpp=[0-9]+/))
			print "bpp=" substr($0, RSTART + 11, RLENGTH - 11)
		seen_mode = 1
		next
	}
	/SB DMA-path decision:/ {
		if (match($0, /is_sb16=[0-9]+/))
			print "is_sb16=" substr($0, RSTART + 8, RLENGTH - 8)
		if (match($0, /dsp_ver=[0-9-]+/))
			print "dsp_ver=" substr($0, RSTART + 8, RLENGTH - 8)
		if (match($0, /highdma=[0-9-]+/))
			print "highdma=" substr($0, RSTART + 8, RLENGTH - 8)
		if (match($0, /-> [0-9A-Za-z-]+ path/)) {
			s = substr($0, RSTART + 3, RLENGTH - 8)
			print "dma_path=" s
		}
		next
	}
	/gus backend ready: GF1 detected/ {
		if (match($0, /[0-9]+ voices/))
			print "gf1_voices=" substr($0, RSTART, RLENGTH - 7)
		if (match($0, /[0-9]+ KB DRAM/))
			print "gf1_dram_kb=" substr($0, RSTART, RLENGTH - 8)
		next
	}
	/opl3 backend ready \(OPL3 detected/ { print "opl3_detected=1"; next }

	END { if (route != "") print "route=" route }
	' "$1" "$2"
}

# inter_flip_ms samples, sorted. Kept out of awk so the median is a plain
# lower-median over a real sort rather than a hand-rolled one.
cell_flips() {
	tr -d '\r' < "$1" \
		| sed -n 's/.*flip-probe: flip #.*inter_flip_ms=\([0-9][0-9]*\).*/\1/p' \
		| sort -n
}

# ---------------------------------------------------------------------------
# nfo_scan DIR
#
# The sweep BATs drop a LOGS\<M><SWEEP>.NFO manifest beside the logs carrying
# cpu / log_tag_prefix / sound / video. That is a declared hardware witness
# rather than one inferred from log lines, so where it exists it outranks the
# inference. The format is not yet fixed, so parse defensively: tolerate CRLF,
# surrounding spaces, any key case, comment lines and unknown keys, and treat
# a missing or unparseable .NFO as simply absent rather than as an error.
# Emits: <prefix>\t<cpu>\t<sound>\t<video> per .NFO found.
# ---------------------------------------------------------------------------
nfo_scan() {
	for n in "$1"/*.NFO "$1"/*.nfo; do
		[ -f "$n" ] || continue
		awk -v fallback="$(basename "$n" | sed 's/\.[Nn][Ff][Oo]$//')" '
		{ sub(/\r$/, "") }
		/^[[:space:]]*[#;]/ { next }
		{
			line = $0
			eq = index(line, "=")
			if (eq == 0) next
			k = substr(line, 1, eq - 1)
			v = substr(line, eq + 1)
			gsub(/^[[:space:]]+|[[:space:]]+$/, "", k)
			gsub(/^[[:space:]]+|[[:space:]]+$/, "", v)
			gsub(/^"|"$/, "", v)
			lk = tolower(k)
			if (lk == "log_tag_prefix" || lk == "logtagprefix" || lk == "prefix") pfx = v
			else if (lk == "cpu") cpu = v
			else if (lk == "sound" || lk == "audio") snd = v
			else if (lk == "video" || lk == "card") vid = v
		}
		END {
			if (pfx == "") pfx = fallback
			if (cpu == "" && snd == "" && vid == "") exit
			printf "%s\t%s\t%s\t%s\n", pfx, (cpu=="")?"-":cpu, (snd=="")?"-":snd, (vid=="")?"-":vid
		}' "$n"
	done
}

# ---------------------------------------------------------------------------
# Per-round analysis
# ---------------------------------------------------------------------------
ROUND_N=0
for DIR in "$@"; do
	[ -d "$DIR" ] || die "not a directory: $DIR"

	SRC="$DIR"
	if ! ls "$SRC"/*.LOG >/dev/null 2>&1; then
		if [ -d "$SRC/logs" ] && ls "$SRC/logs"/*.LOG >/dev/null 2>&1; then
			SRC="$SRC/logs"
		else
			die "no *.LOG files in $DIR (nor $DIR/logs)"
		fi
	fi

	ROUND_N=$((ROUND_N + 1))
	REC="$TMP/rec.$ROUND_N"
	: > "$REC"
	NCELL=0
	NFO="$TMP/nfo.$ROUND_N"
	{ nfo_scan "$SRC"; nfo_scan "$DIR"; } 2>/dev/null | sort -u > "$NFO" || : > "$NFO"
	NNFO=$(wc -l < "$NFO" | tr -d ' ')

	for MAIN in "$SRC"/*.LOG; do
		case "$MAIN" in *SDL.LOG) continue ;; esac
		TAG=$(basename "$MAIN" .LOG)
		SDL="$SRC/${TAG}SDL.LOG"
		[ -f "$SDL" ] || SDL=/dev/null

		K="$TMP/k"
		cell_scan "$MAIN" "$SDL" > "$K"

		get() { sed -n "s/^$1=//p" "$K" | head -1; }

		flips=$(get flips)
		render_s=$(get render_s)
		backend=$(get backend)
		route=$(get route)
		wave_tag=$(get wave_tag)
		seed=$(get seed)
		end_tick=$(get end_tick)
		exit_code=$(get exit_code)
		pxtcache=$(get pxtcache)
		vram_kb=$(get vram_kb)
		lfb_guard=$(get lfb_guard)
		mode=$(get mode)
		res=$(get res)
		fb_base=$(get fb_base)
		is_sb16=$(get is_sb16)
		dsp_ver=$(get dsp_ver)
		dma_path=$(get dma_path)
		gf1_voices=$(get gf1_voices)
		gf1_dram_kb=$(get gf1_dram_kb)

		# --- resolve provenance BY SCHEMA ---------------------------
		# binary_sha12 means two different things depending on version:
		#
		#   v1  binary_sha12 is NOT a binary hash. It has been described
		#       both as the Organya cache key and as a source-diff
		#       fingerprint; this tool does not adjudicate between those
		#       and reports it under the neutral name provenance_key.
		#       What matters is the invariant both agree on: the real
		#       binary sha is NOT recorded anywhere in a v1 log, so this
		#       tool must never print one. Quoting this value as a
		#       "binary sha" is the exact defect v2 exists to fix.
		#   v2  binary_sha12 = the real binary sha; the v1 payload moves
		#       to organya_cache_key (and/or binary_sha12_src).
		#
		# An absent or unrecognised schema_version is treated as v1.
		schema=$(get schema)
		sha12_raw=$(get sha12_raw)
		sha12_src=$(get sha12_src)
		org_key_v2=$(get org_key_v2)
		case "$schema" in
		2)
			binary_sha="${sha12_raw:--}"
			org_key="${org_key_v2:-${sha12_src:--}}"
			;;
		1|"")
			[ -z "$schema" ] && schema=1
			binary_sha="-"
			org_key="${sha12_raw:--}"
			;;
		*)
			# Unknown future schema: carry it, do not guess, do not die.
			binary_sha="-"
			org_key="${org_key_v2:-${sha12_src:-${sha12_raw:--}}}"
			;;
		esac

		# Declared witness from the sweep manifest, if one covers this tag.
		# Longest matching log_tag_prefix wins so a specific sweep beats a
		# general one.
		nfo_cpu=-; nfo_snd=-; nfo_vid=-; nfo_pfx=""
		if [ "$NNFO" -gt 0 ]; then
			while IFS="$(printf '\t')" read -r p c sd vd; do
				[ -n "$p" ] || continue
				case "$TAG" in
				"$p"*)
					if [ ${#p} -gt ${#nfo_pfx} ]; then
						nfo_pfx="$p"; nfo_cpu="$c"; nfo_snd="$sd"; nfo_vid="$vd"
					fi
					;;
				esac
			done < "$NFO"
		fi

		pumped=0
		grep -q '^pump_opl=1$' "$K" && pumped=1
		grep -q '^pump_pit=1$' "$K" && pumped=1

		# --- metric -------------------------------------------------
		if [ -n "$flips" ] && [ -n "$render_s" ]; then
			per_loop=$(awk -v f="$flips" -v s="$REEL_S" \
				'BEGIN { printf "%.2f", f / s }')
			overhead=$((render_s - REEL_S))
		else
			per_loop=NO_FPSTRUE
			overhead=NO_FPSTRUE
			flips=-
			render_s=-
		fi

		# --- inter_flip median --------------------------------------
		cell_flips "$MAIN" > "$TMP/if"
		nif=$(wc -l < "$TMP/if" | tr -d ' ')
		if [ "$nif" -gt 0 ]; then
			med=$(sed -n "$(((nif + 1) / 2))p" "$TMP/if")
			inband=$(awk '$1 >= 10 && $1 <= 40' "$TMP/if" | wc -l | tr -d ' ')
		else
			med=""
			inband=0
		fi

		# GATE 2 enforcement: a pumped cell has no valid inter_flip
		# median to emit, at all, ever. The pump reprograms PIT ch0,
		# which is what DJGPP uclock() reads, so SDL's whole timebase
		# is corrupted and the samples quantize to the 55 ms BIOS tick.
		if [ "$pumped" -eq 1 ]; then
			ifcol=INVALID_PUMP
		elif [ -z "$med" ]; then
			ifcol=-
		else
			ifcol="${med}ms"
		fi

		# --- route --------------------------------------------------
		if [ "$route" = "$ROUTE_FULL" ]; then
			routecol=full
		elif [ -z "$route" ]; then
			routecol=NONE
		else
			routecol="TRUNC($(echo "$route" | wc -w | tr -d ' ')/11)"
		fi

		# --- witnesses ----------------------------------------------
		case "$vram_kb" in
		1024) card="Cirrus-CL-GD5430?" ;;
		2048) card="ATI-Mach64?" ;;
		4096) card="S3-ViRGE?" ;;
		"")   card="-" ;;
		*)    card="vram${vram_kb}?" ;;
		esac
		audio="${dma_path:--}"
		[ -n "$gf1_voices" ] && audio="GF1-${gf1_voices}v"

		printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
			"$TAG" "${backend:--}" "$per_loop" "$overhead" \
			"$flips" "$render_s" "$ifcol" "$routecol" \
			"$card" "$audio" "$pumped" "$nif" "$inband" "$med" \
			"${wave_tag:--}" "$org_key" "${seed:--}" \
			"${end_tick:--}" "${exit_code:--}" "${pxtcache:--}" \
			"${vram_kb:--}:${fb_base:--}:${mode:--}:${res:--}:${lfb_guard:--}" \
			"${is_sb16:--}:${dsp_ver:--}:${dma_path:--}:${gf1_voices:--}:${gf1_dram_kb:--}" \
			"$route" "$schema" "$binary_sha" \
			"${nfo_cpu}:${nfo_snd}:${nfo_vid}" >> "$REC"
		NCELL=$((NCELL + 1))
	done

	echo "$SRC" > "$TMP/src.$ROUND_N"

	# ------------------------------------------------------------------
	# Report
	# ------------------------------------------------------------------
	{
	echo
	echo "=== ROUND: $SRC  ($NCELL cells)"
	echo
	} >&3

	# Band assignment: sort by per-loop fps descending, then group cells
	# within BAND_FPS of the band leader. Differences INSIDE a band are
	# inside the pre-reel-title-flip caveat and are not differences.
	sort -t "$(printf '\t')" -k3,3gr "$REC" \
		| awk -F "$(printf '\t')" -v band="$BAND_FPS" '
		BEGIN { OFS = FS; b = 0; lead = "" }
		{
			v = $3 + 0
			if ($3 !~ /^[0-9.]+$/) { $(NF + 1) = "-" }
			else {
				if (lead == "" || (lead - v) > band) { b++; lead = v }
				$(NF + 1) = sprintf("%c", 96 + b)
			}
			print
		}' > "$TMP/sorted.$ROUND_N"

	if [ "$FORMAT" = csv ]; then
		# Same column order as qa-results/ROUND1-CELLS.csv so the two are
		# interchangeable. cpu comes from the .NFO manifest when one covers
		# the cell; a v1 round with no .NFO cannot witness it and emits "-".
		[ "$ROUND_N" = 1 ] && printf 'cell,cpu,dataset,backend,music_pump,flips,render_s,per_loop_fps,overhead_s,median_fps,route\n'
		awk -F "$(printf '\t')" -v ds="$(basename "$DIR")" '
		$3 !~ /^[0-9.]+$/ { next }   # no [fps-true]: no row, as upstream does
		{
			split($26, n, ":")
			cpu = (n[1] == "" ? "-" : n[1])
			med = ($11 == 1) ? "NA_pump" : ($14 == "" ? "-" : sprintf("%.1f", 1000 / $14))
			route = ($8 == "full") ? "full" : "truncated"
			printf "%s,%s,%s,%s,%d,%s,%s,%s,%s,%s,%s\n", \
				$1, cpu, ds, $2, $11, $5, $6, $3, $4, med, route
		}' "$TMP/sorted.$ROUND_N"
	elif [ "$FORMAT" = tsv ]; then
		printf 'tag\tbackend\tper_loop_fps\toverhead_s\tflips\trender_s\tinter_flip\troute\tcard\taudio\tband\n'
		awk -F "$(printf '\t')" 'BEGIN { OFS = FS }
			{ print $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $NF }' \
			"$TMP/sorted.$ROUND_N"
	else
		{
			printf 'TAG\tBACKEND\tPER-LOOP\tOVHD_S\tFLIPS\tRENDER_S\tINTER_FLIP\tROUTE\tBAND\tCARD\tAUDIO\n'
			awk -F "$(printf '\t')" 'BEGIN { OFS = FS }
				{ print $1, $2, $3, $4, $5, $6, $7, $8, $NF, $9, $10 }' \
				"$TMP/sorted.$ROUND_N"
		} | column -t -s "$(printf '\t')"
	fi

	if [ "$VERBOSE" -eq 1 ]; then
		{
		echo
		echo "--- hardware witnesses (proven from the logs; card names are"
		echo "    INFERRED from VRAM size only -- UNIVBE hides chip identity)"
		while IFS="$(printf '\t')" read -r tag be pl ov fl rs ifc rt cd au pm nif inb med wt ok sd et ec px vidw audw rte sch bs nfow band; do
			IFS=: read -r w_vram w_fb w_mode w_res w_guard <<-EOF
			$vidw
			EOF
			IFS=: read -r a_sb16 a_dsp a_dma a_voices a_dram <<-EOF
			$audw
			EOF
			echo "  $tag ($be)"
			echo "        video: vram=${w_vram}KB fb_base=$w_fb mode=$w_mode res=$w_res cirrus_lfb_guard=$w_guard"
			echo "        audio: is_sb16=$a_sb16 dsp_ver=$a_dsp dma_path=$a_dma gf1_voices=$a_voices gf1_dram=${a_dram}KB"
			case "$bs:$sch" in
			-:1) bsshow="not-recorded-in-v1-logs" ;;
			-:*) bsshow="unknown-schema-v$sch-not-asserted" ;;
			*)   bsshow="$bs" ;;
			esac
			echo "        run:   runmanifest_schema=v$sch binary_sha=$bsshow provenance_key=$ok"
			echo "               reel_seed=$sd reel_end_tick=$et exit_code=$ec pxt_cache=$px"
			echo "        clock: inter_flip n=$nif median=${med:--}ms samples-in-10-40ms-band=$inb"
			[ "$pm" = 1 ] && echo "               ^ PUMPED: median above is CORRUPT, never publish it"
			echo "        route: $rte"
		done < "$TMP/sorted.$ROUND_N"
		} >&3
	fi

	# ------------------------------------------------------------------
	# GATES
	# ------------------------------------------------------------------
	{
	echo
	echo "--- GATES"

	# GATE 1: route truncation.
	bad=$(awk -F "$(printf '\t')" '$8 != "full" { print "      " $1 " route=" $8 " (" $23 ")" }' "$REC")
	if [ -n "$bad" ]; then
		echo "  [FAIL] GATE 1 route truncation -- cell did not finish the reel."
		echo "         Its fps may still be usable; its ROUTE is not."
		echo "$bad"
		FAILED=1
	else
		echo "  [ OK ] GATE 1 route -- all $NCELL cells ran the full 11-stage route."
	fi

	# GATE 2: pump-median poisoning.
	pcells=$(awk -F "$(printf '\t')" '$11 == 1 { printf "%s ", $1 }' "$REC")
	npump=$(awk -F "$(printf '\t')" '$11 == 1' "$REC" | wc -l | tr -d ' ')
	leak=$(awk -F "$(printf '\t')" '$11 == 1 && $7 != "INVALID_PUMP" { print $1 }' "$REC")
	if [ -n "$leak" ]; then
		echo "  [FAIL] GATE 2 pump-median poisoning -- a pumped cell emitted a median."
		echo "         This is a bug in this script. Cells: $leak"
		FAILED=1
	elif [ "$npump" -gt 0 ]; then
		echo "  [ OK ] GATE 2 pump-median poisoning -- ENFORCED, $npump cells suppressed:"
		echo "         $pcells"
		echo "         Their inter_flip_ms is INVALID (PIT ch0 reprogrammed ="
		echo "         DJGPP uclock()'s source = SDL's timebase). Use per-loop fps."
	else
		echo "  [ OK ] GATE 2 pump-median poisoning -- no pumped cells in this round."
	fi

	# GATE 2b: an UNDETECTED pump. Independent of the banner text: a cell
	# with no pump marker whose samples avoid the 10-40 ms band entirely and
	# whose median sits near the 55 ms BIOS tick is pumped by something this
	# script does not recognise. Fail closed rather than publish that median.
	sus=$(awk -F "$(printf '\t')" \
		'$11 == 0 && $12 > 20 && $13 == 0 && $14 >= 45 { print "      " $1 " median=" $14 "ms in-band=0 of " $12 " samples" }' "$REC")
	if [ -n "$sus" ]; then
		echo "  [FAIL] GATE 2b UNDETECTED pump -- no pump banner, but the timebase"
		echo "         shows the pump signature (empty 10-40ms band, median ~55ms)."
		echo "$sus"
		FAILED=1
	else
		echo "  [ OK ] GATE 2b no undetected pump signature."
	fi

	# GATE 3: mixed provenance in one round.
	g3=0
	# Mixing schemas inside one round is itself a provenance failure: the
	# fields do not mean the same thing on both sides of the version bump,
	# so no cross-schema comparison below can be trusted.
	schemas=$(awk -F "$(printf '\t')" '{ print $24 }' "$REC" | sort -u)
	if [ "$(echo "$schemas" | grep -c .)" -gt 1 ]; then
		echo "  [FAIL] GATE 3 mixed RUNMANIFEST schema versions in one round:"
		echo "$schemas" | sed 's/^/         v/'
		echo "         binary_sha12 means different things across this"
		echo "         boundary -- re-run the round on one binary."
		g3=1; FAILED=1
	fi
	for fld in 16:provenance_key 25:binary_sha 17:header_seed 18:reel_end_tick; do
		col=${fld%%:*}; name=${fld##*:}
		vals=$(awk -F "$(printf '\t')" -v c="$col" '$c != "-" { print $c }' "$REC" | sort -u)
		n=$(echo "$vals" | grep -c . )
		if [ "$n" -gt 1 ]; then
			echo "  [FAIL] GATE 3 mixed provenance -- $n distinct $name in one round:"
			echo "$vals" | sed 's/^/         /'
			g3=1; FAILED=1
		fi
	done
	# Stale-log detector: a log whose runmanifest tag disagrees with its
	# filename was left over from an earlier iter and never overwritten.
	stale=$(awk -F "$(printf '\t')" '$15 != "-" && $15 != $1 { print "         " $1 ".LOG carries wave_tag=" $15 }' "$REC")
	if [ -n "$stale" ]; then
		echo "  [FAIL] GATE 3 stale log -- filename disagrees with runmanifest tag:"
		echo "$stale"
		g3=1; FAILED=1
	fi
	if [ "$g3" -eq 0 ]; then
		sch1=$(echo "$schemas" | head -1)
		if [ "$sch1" = 1 ]; then
			echo "  [ OK ] GATE 3 provenance -- v1 round: one provenance key,"
			echo "         one reel, no stale logs. A v1 log does NOT record the"
			echo "         binary sha, so this tool reports none for this round."
		else
			echo "  [ OK ] GATE 3 provenance -- v$sch1 round: one binary sha, one"
			echo "         provenance key, one reel, no stale logs."
		fi
	fi

	echo
	} >&3
done

# ---------------------------------------------------------------------------
# GATE 4: duplicate tag across rounds with differing hardware witnesses.
#
# The 17-cell collision: the PG and VB sweeps both write tag 17, so running
# one after the other without pulling and clearing LOGS\ silently overwrites
# the first. Within a single round directory the overwrite is invisible by
# construction -- it only shows up when the same tag appears in two rounds
# with different hardware underneath it.
# ---------------------------------------------------------------------------
if [ "$ROUND_N" -gt 1 ]; then
	{
	echo "=== CROSS-ROUND GATES"
	echo
	ALL="$TMP/all"
	: > "$ALL"
	i=1
	while [ "$i" -le "$ROUND_N" ]; do
		src=$(cat "$TMP/src.$i")
		awk -F "$(printf '\t')" -v s="$src" 'BEGIN { OFS = FS }
			{
				# A declared manifest witness outranks one inferred from
				# log lines; fall back to inference where no .NFO covers
				# the cell.
				if ($26 != "" && $26 != "-:-:-")
					w = "nfo[" $26 "]"
				else
					w = "log[" $21 ":" $22 "]"
				print $1, w, s
			}' "$TMP/rec.$i" >> "$ALL"
		i=$((i + 1))
	done

	dup=$(sort "$ALL" | awk -F "$(printf '\t')" '
		{ if ($1 in wit) { if (wit[$1] != $2) bad[$1] = 1 }
		  else wit[$1] = $2
		  rec[$1] = rec[$1] "\n         " $3 " -> " $2 }
		END { for (t in bad) print "      tag " t ":" rec[t] }')
	if [ -n "$dup" ]; then
		echo "  [FAIL] GATE 4 duplicate tag with DIFFERING hardware witnesses."
		echo "         Same tag, different hardware = one of these cells is not"
		echo "         what its name says. Check whether a later sweep overwrote"
		echo "         an earlier one without LOGS\\ being cleared in between."
		echo "$dup"
		FAILED=1
	else
		echo "  [ OK ] GATE 4 no duplicate tag carries differing hardware witnesses."
	fi
	echo
	} >&3
fi

{
echo "--- NOTES"
echo "  per-loop fps = flips / $REEL_S      overhead_s = render_s - $REEL_S"
echo "  Cells sharing a BAND letter are within $BAND_FPS fps of the band leader."
echo "  A within-band difference is inside the pre-reel title-flip caveat and"
echo "  MUST NOT be reported as a difference."
echo "  Card names are inferred from VRAM size; UNIVBE rewrites the OEM strings.
  v1 RUNMANIFEST logs do NOT record a binary sha (binary_sha12 holds a
  provenance key, not a hash); no binary sha is reported for a v1 round."

} >&3

if [ "$FAILED" -ne 0 ]; then
	{
	echo
	echo "$PROG: one or more gates FAILED -- do not publish these numbers as-is."
	} >&3
	exit 1
fi
exit 0
