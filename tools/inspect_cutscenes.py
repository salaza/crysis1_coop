import zipfile, xml.etree.ElementTree as ET

pak_path = 'E:/SteamLibrary/steamapps/common/Crysis/Game/Levels/village/level.pak'
with zipfile.ZipFile(pak_path, 'r') as z:
    data = z.read('mission_mission0.xml')
root = ET.fromstring(data)

for fg in root.findall('.//FlowGraph'):
    fg_str = ET.tostring(fg, encoding='unicode')
    if 'cine_1_villageIntro' in fg_str:
        print('=== FlowGraph with cine_1_villageIntro ===')
        nodes = {}
        for node in fg.findall('.//Node'):
            nid = node.attrib.get('Id')
            nclass = node.attrib.get('Class')
            inputs = {}
            for inp in node.findall('.//Inputs'):
                inputs.update(inp.attrib)
            nodes[nid] = (nclass, inputs)
            if 'cine_1' in str(inputs) or 'PlaySequence' in nclass:
                print(f'Node {nid}: Class={nclass}, Inputs={inputs}')
        
        for e in fg.findall('.//Edge'):
            no = e.attrib.get('nodeOut')
            ni = e.attrib.get('nodeIn')
            if no in nodes and ni in nodes:
                if 'PlaySequence' in nodes[no][0] or 'PlaySequence' in nodes[ni][0]:
                    print(f"Edge: {no} ({nodes[no][0]}):{e.attrib.get('portOut')} -> {ni} ({nodes[ni][0]}):{e.attrib.get('portIn')}")
