import os, sys, zipfile, subprocess, urllib.request, json

def get_github_token():
    p = subprocess.Popen(['git', 'credential', 'fill'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    out, _ = p.communicate('protocol=https\nhost=github.com\n\n')
    for line in out.splitlines():
        if line.startswith('password='):
            return line.split('=', 1)[1].strip()
    return None

def package_zip(src_dir, zip_path):
    print(f"Creating release archive {zip_path} from {src_dir}...")
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as z:
        for root, dirs, files in os.walk(src_dir):
            for file in files:
                full_path = os.path.join(root, file)
                rel_path = os.path.relpath(full_path, src_dir)
                z.write(full_path, rel_path)
    size_mb = os.path.getsize(zip_path) / (1024 * 1024)
    print(f"Archive created! Size: {size_mb:.2f} MB")

def publish_github_release(repo, tag, name, body, zip_path, token):
    headers = {
        'Authorization': f'token {token}',
        'Accept': 'application/vnd.github.v3+json',
        'User-Agent': 'CrysisCoopReleasePublisher'
    }

    # 1. Create Release
    create_url = f'https://api.github.com/repos/{repo}/releases'
    payload = {
        'tag_name': tag,
        'target_commitish': 'main',
        'name': name,
        'body': body,
        'draft': False,
        'prerelease': False
    }
    req = urllib.request.Request(create_url, data=json.dumps(payload).encode('utf-8'), headers=headers, method='POST')
    try:
        with urllib.request.urlopen(req) as resp:
            rel_data = json.loads(resp.read().decode('utf-8'))
            print(f"[OK] GitHub release created: {rel_data.get('html_url')}")
            upload_url = rel_data['upload_url'].split('{')[0]
    except urllib.error.HTTPError as e:
        print(f"Failed to create release: {e.code} {e.read().decode('utf-8')}")
        # If release already exists, fetch it
        fetch_url = f'https://api.github.com/repos/{repo}/releases/tags/{tag}'
        req_fetch = urllib.request.Request(fetch_url, headers=headers)
        with urllib.request.urlopen(req_fetch) as resp:
            rel_data = json.loads(resp.read().decode('utf-8'))
            upload_url = rel_data['upload_url'].split('{')[0]
            print(f"[OK] Found existing release: {rel_data.get('html_url')}")

    # 2. Upload Asset
    asset_name = os.path.basename(zip_path)
    upload_url_full = f"{upload_url}?name={asset_name}"
    headers['Content-Type'] = 'application/zip'
    with open(zip_path, 'rb') as f:
        zip_bytes = f.read()

    print(f"Uploading asset {asset_name} ({len(zip_bytes)/(1024*1024):.2f} MB)...")
    req_upload = urllib.request.Request(upload_url_full, data=zip_bytes, headers=headers, method='POST')
    with urllib.request.urlopen(req_upload) as resp:
        asset_data = json.loads(resp.read().decode('utf-8'))
        print(f"[OK] Asset uploaded successfully: {asset_data.get('browser_download_url')}")

if __name__ == '__main__':
    src = r'E:\SteamLibrary\steamapps\common\Crysis\Mods\CrysisCoop'
    tag = 'v1.7.2'
    zip_name = f'CrysisCoop_Release_{tag}.zip'
    package_zip(src, zip_name)

    token = get_github_token()
    if not token:
        print("Error: Could not retrieve GitHub token!")
        sys.exit(1)

    release_body = """### Crysis Co-op v1.7.2 Release Notes

#### UI & Flash Fixes
- **FrontEnd Mods**: Added missing `Mods.gfx` and `Mods.swf` to resolve GFxLoader error in menus.
- **HUD Grenade Detection**: Checked stage instantiation of `GrenadeDetect` to eliminate invalid `gotoAndPlay` method call errors in `HUD_GrenadeDetect.gfx` and `HUD_GrenadeDetect_Friendly.gfx`.
- **SP HexIcons**: Guarded PowerStruggle icon calls (`setHexIcon`) in singleplayer/co-op mode to prevent missing method errors in `HUD_SP_HexIcons.gfx`.
- **Stealth Bar**: Validated `StealthBar` MovieClip availability before calling `gotoAndStop` in `HUD_RadarCompassStealth.gfx`.

#### Tactical Map & Drawing
- **Squad on Tacmap & Radar**: Populated teammates in co-op mode so all squad members appear on both the Tactical PDA Map and Mini-Radar with real-time positions, headings, and friendly status.
- **PDA Map Tactical Drawing**: Integrated native ActionScript vector drawing canvas inside the map graphics for real-time synchronized tactical drawing and double-click clearing.

#### Sharks & Ocean Overhaul
- **Ocean Shark Behaviour**: Sharks no longer get beached or dragged onto land; sharks in shallow water or out of water despawn immediately.
- **Deep Ocean Spawning**: Sharks only appear when swimming far out into the deep open ocean (>25m water depth), matching singleplayer out-of-bounds predator behaviour. Limited to at most 1 active shark.

#### Weapons, Attachments & Interaction
- **Weapons & Tactical Attachment**: Added `TacticalAttachment` (with 15 `tacbullet` sleep darts), `GrenadeLauncher`, `AssaultScope`, and `SniperScope` to default co-op loadout.
- **Weapon Replacing & Attachment Harvesting**: Fixed bug where holding full ammunition prevented picking up or swapping weapons on the ground even when the ground weapon had uncollected scopes or attachments.
- **Secondary Objective Interaction (Recovery Map 2)**: Enabled interactivity for `InteractiveEntity` laptops and objects in co-op mode, displaying `@use` prompts and allowing objective progression.

#### Admin Menu Cheats & Quality of Life
- **Authoritative Cheats**: Implemented direct server-side execution for God Mode, Unlimited Ammo, Give All Weapons & Attachments, Refill Ammo, AI Ignore Players, and Kill All Enemies.
- **Admin Menu Controls**: Menu options execute strictly via Enter (Space bar no longer triggers actions).
- **Checkpoint Respawns**: Fallen players and squad wipes now revive and respawn at the latest captured checkpoint rather than level start."""

    publish_github_release('salaza/crysis1_coop', tag, f'Crysis Co-op {tag}', release_body, zip_name, token)
    print("Release v1.7.2 successfully published!")
