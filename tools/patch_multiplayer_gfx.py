import zlib, struct, os, shutil

def patch_multiplayer(input_path, output_paths):
    with open(input_path, 'rb') as f:
        data = f.read()

    sig = data[:3]
    is_compressed = sig in (b'CFX', b'CWS')
    if is_compressed:
        raw = data[:8] + zlib.decompress(data[8:])
    else:
        raw = data

    pos = 8
    nbits = raw[pos] >> 3
    nbits_total = 5 + nbits * 4
    nbytes = (nbits_total + 7) // 8
    pos += nbytes
    frame_rate = struct.unpack('<H', raw[pos:pos+2])[0]
    pos += 2
    frame_count = struct.unpack('<H', raw[pos:pos+2])[0]
    pos += 2

    header_bytes = raw[:pos]

    tags = []
    while pos < len(raw):
        tag_header = struct.unpack('<H', raw[pos:pos+2])[0]
        pos += 2
        tag_type = tag_header >> 6
        tag_len = tag_header & 0x3F
        if tag_len == 0x3F:
            tag_len = struct.unpack('<I', raw[pos:pos+4])[0]
            pos += 4
        tag_data = raw[pos:pos+tag_len]
        tag_len = len(tag_data)
        tags.append((tag_type, tag_len, tag_data))
        pos += tag_len

    print(f'Parsed {len(tags)} tags.')

    # 1. Prepare Release Action for Btn_01
    # We take the Release Action from Tag 59 (Btn_03 / LAN Game)
    # which jumps to "networkgame" and sets Section = 3.
    t59_type, t59_len, t59_data = tags[59]
    # Release action in Tag 59 starts at offset 199
    release_action_btn3 = t59_data[199:]
    # Replace MultiplayerBtn3 with MultiplayerBtn1
    release_action_btn1 = release_action_btn3.replace(b'MultiplayerBtn3', b'MultiplayerBtn1')
    assert len(release_action_btn1) == len(release_action_btn3), "Length mismatch in release action!"

    new_tags = []
    for idx, (t_type, t_len, t_data) in enumerate(tags):
        # Tag 167: DefineEditText cid 157 (Btn_01 label: "@ui_menu_QUICKGAME" -> "CO-OP PLAY")
        if idx == 167 and t_type == 37:
            new_data = t_data[:30] + b'CO-OP PLAY\x00'
            new_tags.append((t_type, len(new_data), new_data))
            print(f'Patched Tag 167: len {t_len} -> {len(new_data)}')

        # Tag 47: DefineEditText cid 37 (Btn_01 hint: "@ui_menu_JOIN WELL-POPULATED" -> "PLAY CO-OP CAMPAIGN")
        elif idx == 47 and t_type == 37:
            new_data = t_data[:31] + b'PLAY CO-OP CAMPAIGN\x00'
            new_tags.append((t_type, len(new_data), new_data))
            print(f'Patched Tag 47: len {t_len} -> {len(new_data)}')

        # Tag 61: DefineButton2 cid 51 (Btn_01 initial state)
        elif idx == 61 and t_type == 34:
            new_data = t_data[:154] + release_action_btn1
            new_tags.append((t_type, len(new_data), new_data))
            print(f'Patched Tag 61: len {t_len} -> {len(new_data)}')

        # Tag 255: DefineButton2 cid 245 (Btn_01 active state)
        elif idx == 255 and t_type == 34:
            new_data = t_data[:867] + release_action_btn1
            new_tags.append((t_type, len(new_data), new_data))
            print(f'Patched Tag 255: len {t_len} -> {len(new_data)}')

        else:
            new_tags.append((t_type, t_len, t_data))

    # Reconstruct raw SWF
    out_body = bytearray()
    for t_type, t_len, t_data in new_tags:
        assert len(t_data) == t_len
        if t_len < 63:
            hdr = (t_type << 6) | t_len
            out_body.extend(struct.pack('<H', hdr))
        else:
            hdr = (t_type << 6) | 0x3F
            out_body.extend(struct.pack('<H', hdr))
            out_body.extend(struct.pack('<I', t_len))
        out_body.extend(t_data)

    decompressed_len = len(header_bytes) + len(out_body)
    new_header = bytearray(header_bytes)
    # Write decompressed file length into header[4:8]
    new_header[4:8] = struct.pack('<I', decompressed_len)

    if is_compressed:
        compressed_payload = zlib.compress(bytes(new_header[8:]) + bytes(out_body), 9)
        final_file = new_header[:8] + compressed_payload
    else:
        final_file = new_header + out_body

    for p in output_paths:
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as f:
            f.write(final_file)
        print(f'Wrote patched file to {p} ({len(final_file)} bytes).')

if __name__ == '__main__':
    src = 'Game/Libs/UI/FrontEnd/MultiPlayer.gfx'
    destinations = [
        'Game/Libs/UI/FrontEnd/MultiPlayer.gfx',
        'Game/Libs/UI/FrontEnd/MultiPlayer.swf',
        'Mods/CrysisCoop/Game/Libs/UI/FrontEnd/MultiPlayer.gfx',
        'build/release_staging/Game/Libs/UI/FrontEnd/MultiPlayer.gfx',
    ]
    steam_path = r'E:\SteamLibrary\steamapps\common\Crysis\Mods\CrysisCoop\Game\Libs\UI\FrontEnd\MultiPlayer.gfx'
    if os.path.exists(os.path.dirname(steam_path)):
        destinations.append(steam_path)

    patch_multiplayer(src, destinations)
