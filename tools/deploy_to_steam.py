import os, shutil

def deploy():
    steam_mod_dir = r'E:\SteamLibrary\steamapps\common\Crysis\Mods\CrysisCoop'
    
    # 1. DLLs
    bin64_src = r'build\Mods\CrysisCoop\Bin64\Release\CrysisCoop.dll'
    bin32_src = r'build\Mods\CrysisCoop\Bin32\Release\CrysisCoop.dll'
    
    # Workspace DLLs
    shutil.copy2(bin64_src, r'Bin64\CrysisCoop.dll')
    shutil.copy2(bin32_src, r'Bin32\CrysisCoop.dll')
    print("Copied DLLs to workspace Bin64 and Bin32")
    
    # Steam DLLs
    os.makedirs(os.path.join(steam_mod_dir, 'Bin64'), exist_ok=True)
    os.makedirs(os.path.join(steam_mod_dir, 'Bin32'), exist_ok=True)
    shutil.copy2(bin64_src, os.path.join(steam_mod_dir, r'Bin64\CrysisCoop.dll'))
    shutil.copy2(bin32_src, os.path.join(steam_mod_dir, r'Bin32\CrysisCoop.dll'))
    print("Copied DLLs to Steam CrysisCoop Bin64 and Bin32")
    
    # 2. Version file
    shutil.copy2('version.txt', os.path.join(steam_mod_dir, 'version.txt'))
    print("Copied version.txt")
    
    # 3. Game scripts and XMLs
    files_to_sync = [
        r'Game\Scripts\Entities\Boids\Crabs.lua',
        r'Game\Scripts\Entities\Triggers\ProximityTrigger.lua',
        r'Game\Scripts\Entities\Triggers\AreaTrigger.lua',
        r'Game\Scripts\Entities\Vehicles\Implementations\Xml\US_ltv.xml',
        r'Game\Scripts\Entities\Vehicles\Implementations\Xml\Asian_ltv.xml',
        r'Game\Scripts\Entities\Vehicles\Implementations\Xml\Asian_truck.xml',
        r'Game\Scripts\Entities\Vehicles\Implementations\Xml\Asian_patrolboat.xml',
        r'Game\Scripts\Entities\Vehicles\Implementations\Xml\US_smallboat.xml',
    ]
    
    for rel_path in files_to_sync:
        if os.path.exists(rel_path):
            dst = os.path.join(steam_mod_dir, rel_path)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(rel_path, dst)
            print(f"Synced {rel_path} -> Steam")
            
    print("[SUCCESS] All files deployed to Steam successfully!")

if __name__ == '__main__':
    deploy()
