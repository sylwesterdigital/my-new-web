# setup.py
from pathlib import Path
from setuptools import setup

APP = ['gui_client.py']
VERSION = Path('VERSION').read_text(encoding='utf-8').strip()

OPTIONS = dict(
    argv_emulation=False,  # IMPORTANT: avoid Carbon crash
    iconfile='assets/icon.icns',
    excludes=['pip','setuptools','wheel','pkg_resources'],
    plist=dict(
        CFBundleName='Tiny Text Client',
        CFBundleDisplayName='Tiny Text Client',
        CFBundleIdentifier='com.sylwesterdigital.tinytextclient',
        CFBundleVersion=VERSION,
        CFBundleShortVersionString=VERSION,
        LSMinimumSystemVersion='11.0',
        NSHighResolutionCapable=True,
    ),
)

setup(app=APP, options={'py2app': OPTIONS})
