"""Build a native Windows x64 plugin against an OBS installation, using Zig.

Set ZIG_EXE and OBS_SOURCE to portable toolchain/source locations, or use
the previously downloaded OBS Sketch tools next to this project.
"""
from pathlib import Path
import os
import struct
import subprocess
import shutil
import json
import runpy
import re
import hashlib

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
PREVIOUS = ROOT.parent / 'OBS Sketch/build-tools'
ZIG = Path(os.environ.get('ZIG_EXE', PREVIOUS / 'zig-x86_64-windows-0.15.2/zig.exe'))
OBS = Path(os.environ.get('OBS_SOURCE', PREVIOUS / 'obs-studio-32.2.2'))
OBS_ROOT = Path(os.environ.get('OBS_ROOT', r'C:\Program Files\obs-studio'))
ENV = dict(os.environ, ZIG_LIB_DIR=str(ZIG.parent/'lib'), ZIG_GLOBAL_CACHE_DIR=str(BUILD / 'zig-cache'), ZIG_LOCAL_CACHE_DIR=str(BUILD / 'zig-local'))

def exports(path):
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    optional = pe + 24
    if struct.unpack_from('<H', data, optional)[0] != 0x20b:
        raise ValueError('OBS must be Windows x64')
    sections = []
    for i in range(count):
        offset = optional + optional_size + 40*i
        virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', data, offset+8)
        sections.append((rva, max(virtual_size, raw_size), raw))
    def file_offset(rva):
        for start, size, raw in sections:
            if start <= rva < start+size:
                return raw+rva-start
        raise ValueError('Invalid PE address')
    directory = file_offset(struct.unpack_from('<I', data, optional+112)[0])
    num_names = struct.unpack_from('<I', data, directory+24)[0]
    names = file_offset(struct.unpack_from('<I', data, directory+32)[0])
    result = []
    for i in range(num_names):
        offset = file_offset(struct.unpack_from('<I', data, names+4*i)[0])
        result.append(data[offset:data.index(b'\0', offset)].decode('ascii'))
    return result

def run(args):
    subprocess.run([str(x) for x in args], cwd=ROOT, env=ENV, check=True)

def main():
    runpy.run_path(str(ROOT/"scripts/localization.py"),run_name="__main__")
    BUILD.mkdir(exist_ok=True)
    version=(ROOT/'VERSION').read_text().strip()
    if not re.fullmatch(r'\d+\.\d+\.\d+',version): raise ValueError('Invalid release version')
    (BUILD/'ticker-version.h').write_text('#define NEXUS_TICKER_RELEASE "'+version+'"\n')
    words=version.replace('.',',')+',0'
    (BUILD/'ticker-version.rc').write_text('''VS_VERSION_INFO VERSIONINFO
FILEVERSION {words}
PRODUCTVERSION {words}
FILEFLAGSMASK 0x3fL
FILEFLAGS 0
FILEOS VOS_NT_WINDOWS32
FILETYPE VFT_DLL
BEGIN
 BLOCK "StringFileInfo"
 BEGIN
  BLOCK "040904b0"
  BEGIN
   VALUE "CompanyName", "OBS NEXUS / DjEmiMas\\0"
   VALUE "FileDescription", "OBS News Ticker NEXUS\\0"
   VALUE "FileVersion", "{version}\\0"
   VALUE "ProductName", "OBS News Ticker NEXUS\\0"
   VALUE "ProductVersion", "{version}\\0"
  END
 END
 BLOCK "VarFileInfo"
 BEGIN
  VALUE "Translation",0x409,1200
 END
END
'''.format(words=words,version=version))
    if not ZIG.is_file() or not (OBS/'libobs/obs.h').is_file():
        raise SystemExit('Set ZIG_EXE to Zig 0.15.2 and OBS_SOURCE to OBS Studio 32.2.2 source directory.')
    BUILD.mkdir(exist_ok=True)
    capitals=json.loads((ROOT/'data/weather-capitals.json').read_text(encoding='utf-8'))['capitals']
    if len(capitals)!=24 or len({c['geonames_id'] for c in capitals})!=24:
        raise ValueError('Expected the 24 distinct selected capitals')
    rows=[]
    for c in capitals:
        if not (-90<=c['latitude']<=90 and -180<=c['longitude']<=180):
            raise ValueError('Invalid capital coordinates')
        fields=','.join(json.dumps(c[k],ensure_ascii=False) for k in ('name','selection','query'))
        rows.append('    {'+fields+','+str(c['latitude'])+','+str(c['longitude'])+'},')
    (ROOT/'src/weather-capitals.h').write_text('/* SPDX-License-Identifier: GPL-2.0-or-later */\n/* Generated from data/weather-capitals.json by scripts/build.py. */\n#pragma once\nstruct weather_capital {const char *name,*selection,*query;double latitude,longitude;};\nstatic const struct weather_capital weather_capitals[]={\n'+'\n'.join(rows)+'\n};\n',encoding='utf-8')

    (BUILD/'obsconfig.h').write_text('#pragma once\n#define OBS_RELEASE_CANDIDATE 0\n#define OBS_BETA 0\n')
    (BUILD/'obs.def').write_text('LIBRARY obs.dll\nEXPORTS\n'+'\n'.join(exports(OBS_ROOT/'bin/64bit/obs.dll'))+'\n')
    run([ZIG,'dlltool','-m','i386:x86-64','-d',BUILD/'obs.def','-l',BUILD/'obs.lib'])
    run([ZIG,'rc','/fo',BUILD/'ticker-contract.res','shared/ticker-contract.rc'])
    cc=[ZIG,'cc','-target','x86_64-windows-gnu','-std=c11','-O2','-Wall','-Wextra','-Werror',
        '-I'+str(OBS/'libobs'),'-I'+str(OBS/'frontend/api'),'-I'+str(BUILD)]
    run(cc+['-shared','src/plugin.c','src/now-playing.c','src/weather.c','src/twitch-observer.c',BUILD/'obs.lib',BUILD/'ticker-contract.res','-lshell32','-lgdi32','-lwinhttp','-o',BUILD/'obs-news-ticker-nexus.dll'])
    run(cc+['tests/twitch-observer.c','src/twitch-observer.c','-o',BUILD/'twitch-observer-test.exe'])
    (BUILD/'twitch-fixture').mkdir(exist_ok=True)
    run(cc+['-shared','tests/twitch-core-fixture.c','-o',BUILD/'twitch-fixture/nexus-twitch-core.dll'])
    run(cc+['-shared','tests/frontend-fixture.c','-o',BUILD/'twitch-fixture/obs-frontend-api.dll'])
    run(cc+['tests/integration.c','src/now-playing.c',BUILD/'obs.lib','-o',BUILD/'integration-test.exe'])
    run(cc+['tests/now-playing-obs.c','src/now-playing.c',BUILD/'obs.lib','-o',BUILD/'now-playing-obs-test.exe'])
    run(cc+['tests/weather.c','src/weather.c','src/now-playing.c',BUILD/'obs.lib','-lwinhttp','-o',BUILD/'weather-test.exe'])
    run(cc+['-shared','tests/playing-real-obs-probe.c','src/now-playing.c',BUILD/'obs.lib','-luser32','-lpsapi','-o',BUILD/'playing-real-obs-probe.dll'])
    playing_headers=ROOT.parent/'OBS Playing/playing-next/include'
    if playing_headers.is_dir():
        run([ZIG,'c++','-target','x86_64-windows-gnu','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(OBS/'libobs'),'-I'+str(BUILD),'-I'+str(playing_headers),'tests/obs-playing-publisher.cpp',BUILD/'obs.lib','-o',BUILD/'obs-playing-publisher-test.exe'])
    plugin=ROOT/'dist/obs-news-ticker-nexus'
    (plugin/'bin/64bit').mkdir(parents=True,exist_ok=True)
    shutil.copy2(BUILD/'obs-news-ticker-nexus.dll',plugin/'bin/64bit')
    shutil.copytree(ROOT/'data',plugin/'data',dirs_exist_ok=True)
    for filename in ('README.md','LICENSE'):
        if (ROOT/filename).exists(): shutil.copy2(ROOT/filename,plugin/filename)
    (BUILD/'build-info.json').write_text(json.dumps({
        'version':version,'target':'Windows x64','compiler':str(ZIG),
        'sdk':str(OBS),'obs_runtime':str(OBS_ROOT),
        'dll_sha256':hashlib.sha256((BUILD/'obs-news-ticker-nexus.dll').read_bytes()).hexdigest()
    },indent=2))
    print('Built native plugin and tests:',plugin)

if __name__=='__main__': main()
