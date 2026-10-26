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
    tag = 'v1.7.3'
    zip_name = f'CrysisCoop_Release_{tag}.zip'
    package_zip(src, zip_name)

    token = get_github_token()
    if not token:
        print("Error: Could not retrieve GitHub token!")
        sys.exit(1)

    release_body = """### Crysis Co-op v1.7.3 Release Notes

#### Main Menu & Co-op Flow Integration
- **CO-OP PLAY Main Menu Button**: Replaced the obsolete GameSpy `QUICK GAME` button with a native, streamlined **`CO-OP PLAY`** button directly inside the `02 MULTIPLAYER` menu.
- **Dedicated Co-op Action**: Clicking `CO-OP PLAY` immediately takes players straight into the Co-op Campaign lobby, scanning LAN and internet co-op servers and providing one-click hosting for the full singleplayer campaign mission rotation.
- **Visual & Localization Polish**: Integrated custom subtitle hint (`PLAY CO-OP CAMPAIGN`) and high-resolution typography matching Crysis's sci-fi aesthetic.
- **C++ FSCommand Handling**: Added robust `MP_CoopPlay` and fallback handlers in `MPHub.cpp` ensuring responsive navigation regardless of GameSpy network service status."""

    publish_github_release('salaza/crysis1_coop', tag, f'Crysis Co-op {tag}', release_body, zip_name, token)
    print(f"Release {tag} successfully published!")
