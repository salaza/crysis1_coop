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
    tag = 'v1.7.7'
    zip_name = f'CrysisCoop_Release_{tag}.zip'
    package_zip(src, zip_name)

    token = get_github_token()
    if not token:
        print("Error: Could not retrieve GitHub token!")
        sys.exit(1)

    release_body = """### Crysis Co-op v1.7.7 Release Notes

#### Tactical Attachment Regenerating Ammunition (Faithful to Singleplayer)
- **Automatic Regenerative Darts**:
  - Implemented server-authoritative regenerative dart timer in `CPlayer::Update` for `tacbullet` (tranquilizer darts) and `tagbullet` (tag darts). When possessing the `TacticalAttachment`, darts automatically regenerate back up to capacity (5 darts, 1 dart every 10 seconds).
  - Suppressed intrusive on-screen ammo pickup messages during passive dart regeneration in `ClSetAmmo` and `ClAddAmmo`.

#### Vehicle Gunner Camera & Control Desynchronization
- **Fixed Gunner Camera Jerk & Oblivion Distortion in Humvee/LTV**:
  - Fixed camera matrix math in `CPlayer::UpdateView` when sitting in vehicle seats. The camera matrix now references the vehicle's dynamic physical world orientation rather than decoupling from the actor, and includes singularity and zero-magnitude safeguards to prevent matrix corruption and `NaN`/`INF` camera flips.
  - In `CVehicleClient::OnAction`, intercepted driver inputs (`v_rotateyaw`, `v_rotatepitch`, `attack1`, etc.) so that when a co-op partner occupies the roof gunner turret, the driver's mouse steering and view never fight or override the gunner's aim.
  - Overrode `US_ltv.xml`, `Asian_ltv.xml`, `Asian_truck.xml`, `Asian_patrolboat.xml`, and `US_smallboat.xml` removing singleplayer `remotelyUseActionsFromSeat="gunner"`, granting the turret passenger exclusive, smooth 360-degree aiming and firing.

#### TimeOfDay (ToD) Full Client Network Synchronization
- **Fixed Host Night vs Client Day Desync**:
  - Resolved an engine-level limitation where CryEngine's `CTimeOfDay::NetSerialize` silently ignored time updates when `sv_timeofdayenable = 0`.
  - Added explicit, unquantized `CoopCurTimeOfDay` serialization in `CGameRules::NetSerialize` across `eEA_GameServerDynamic`, guaranteeing client clocks mirror the server continuously.
  - Added reliable `ClSyncTimeOfDay` RMI broadcast upon client connection, revive, and level transitions to force instant Time of Day synchronization.

#### Grenade Pickups & HUD Synchronization
- **Fixed Missing Grenades on Client HUD**:
  - Fixed an issue where picking up grenades on the server never refreshed the client's off-hand fire mode or HUD icons.
  - In `CWeapon::SetCurrentFireMode`, added reliable `ClSetFireMode` RMI broadcast across network.
  - In `CActor::ClSetAmmo` and `ClAddAmmo`, when grenade ammunition is received and off-hand is out of ammo, client immediately switches fire mode to the newly acquired grenade type and updates the HUD.

#### Random Audio Silence on Host Resolved
- **Fixed Sound Engine Muting / Fading Out**:
  - Singleplayer sound moods (`low_health`, `concentration`, `cloak`, `freeze`, `explosion`) were executing on the server whenever ANY remote client or AI took damage or zoomed, permanently deafening the host's sound system.
  - Restricted all sound mood triggers strictly to `IsClient()` (local client only).
  - Added an automatic watchdog in `CSoundMoods::Update()` that detects and cleans up any orphaned or stale unlimited sound moods.
  - Added an audio watchdog in `CGame::Update` ensuring master sound is never left paused.

#### NPC Weapon Models on Clients Fixed
- **Fixed Clients Only Seeing Pistols on AI Soldiers**:
  - Removed `gEnv->bServer` restriction on `PostInit` and `ENTITY_EVENT_UNHIDE` in `CoopGrunt`, `CoopTrooper`, and `CoopScout`, ensuring clients properly receive and parse equipment packs.
  - Added active weapon network serialization in `NetSerialize` across `ASPECT_ALIVE`, ensuring clients immediately equip and display the exact FY71, Shotgun, Hurricane, or Gauss rifle held by the server AI.

#### Beach Crabs Loud Noise Reduction
- **Silenced Constant Crab Screeching**:
  - Overrode `Crabs.lua` to remove continuous `crab_idle` sound playback across beach areas, eliminating deafening sound spam for other players.

#### Co-op Triggers & Softlock Prevention
- **AreaTrigger & ProximityTrigger Upgraded for Co-op**:
  - Overrode `ProximityTrigger.lua` and `AreaTrigger.lua` to recognize any human co-op player for `bOnlyMyPlayer` and `bOnlyLocalPlayer` checks, allowing any team member to trigger mission gates and objectives.

#### Version Metadata
- Bumped watermark and version metadata to v1.7.7."""

    publish_github_release('salaza/crysis1_coop', tag, f'Crysis Co-op {tag}', release_body, zip_name, token)
    print(f"Release {tag} successfully published!")
