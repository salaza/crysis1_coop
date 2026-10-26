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
                if file.endswith('.old'):
                    continue
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
    tag = 'v1.7.6'
    zip_name = f'CrysisCoop_Release_{tag}.zip'
    package_zip(src, zip_name)

    token = get_github_token()
    if not token:
        print("Error: Could not retrieve GitHub token!")
        sys.exit(1)

    release_body = """### Crysis Co-op v1.7.6 Release Notes

#### Audio Silence & Cutscene Audio Restoration
- **Fixed Complete Audio Silence in School (`village`) & Rosenthal Lab (`rescue`)**:
  - Removed manual listener matrix and VisArea overrides during cutscene playback, allowing CryEngine's `CView` and `CrySoundSystem` to naturally calculate listener position, orientation, and room VisArea from the active camera.
  - Fixed an issue where the standard audio listener retained an interior VisArea lock indefinitely after cutscenes completed or aborted, causing sound occlusion to evaluate all ambient sounds, footsteps, and weapon fire outside that room as 100% occluded (-100 dB silence).
  - Explicitly cleared listener VisArea (`pListener->SetVisArea(NULL)`) and underwater flags upon cutscene exit and level load.
  - Automatically unpauses the sound engine (`gEnv->pSoundSystem->Pause(false, true)`) and clears any lingering sound moods (`ClearAllSoundMoods()`) upon cutscene entry, exit, and map loading.

#### Fix Random Weapons Laying on Ground at NPC Spawns
- **Resolved NPC Weapon Dropping Bug**:
  - Removed destructive `GetInventory()->Clear()` calls on `ENTITY_EVENT_HIDE` in `CoopGrunt`, `CoopTrooper`, and `CoopScout`. Previously, when level FlowGraphs initially disabled/hid grunts, clearing inventory severed the link between the grunt and its weapons without destroying the models, leaving orphaned guns floating or lying loose on the ground.
  - Added checks in `PostInit` and `eTIMER_WEAPONDELAY` to prevent spawning equipment packs for hidden or disabled entities until unhidden by mission triggers.
  - Added duplicate weapon check (`GetInventory()->GetCount() > 0`) preventing unhidden entities from receiving redundant weapons and dropping previous ones.

#### Campaign TimeOfDay (ToD) Speed & Night/Day Progression
- **Disabled Rapid Multiplayer Time Cycling**:
  - Automatically forces `sv_timeofdayenable = 0` upon map loading, disabling CryEngine's multiplayer clock that was rushing through a 24-hour cycle every 1–2 hours.
  - Campaign TimeOfDay now faithfully follows the designer-crafted `TimeAnimSpeed` (0.001 to 0.02) and FlowGraph curves, perfectly preserving singleplayer lighting progression.
- **Dynamic Level Start Time Initialization**:
  - Automatically queries and applies each level's designer-intended `fStartTime` on load (e.g. 5:30 AM for `island`, 10:00 AM for `village`, 4:00 PM for `rescue`, 5:00 AM for `harbor`, 9:00 AM for `tank`).
- **Mission 1 (`island`) Night Start Fixed**:
  - Corrected `Time="8.1700001"` in `Game/Levels/island/mission_mission0.xml` to `Time="5.5"`. `island` now starts at 05:30 AM (pitch dark with moon and flashlight), ensuring the Aztec corpse discovery sequence occurs in the dark as originally designed.

#### Mission 1 Starting Gear & Tactical Attachment
- **Restored Tactical Attachment on SCAR**:
  - Restored `TacticalAttachment` to `Player_Island_Full.xml` equipment pack.
  - Granted starting ammunition for `tacbullet` (15 sleep darts) and `tagbullet` (15 tracker darts) upon spawning.
  - Ensured `Coop:EquipPlayer` in `Coop.lua` supplies `tagbullet` and `tacbullet` ammunition upon player revive.

#### Version Metadata
- Bumped watermark and version metadata to v1.7.6."""

    publish_github_release('salaza/crysis1_coop', tag, f'Crysis Co-op {tag}', release_body, zip_name, token)
    print(f"Release {tag} successfully published!")
