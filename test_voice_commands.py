from host_tools import CC, CXX, SHARED_EXPORT, unload_library
"""Compile the production parser; exercise requests, ambiguity and negation."""
import ctypes
import _ctypes
import subprocess
import tempfile
from pathlib import Path
root = Path(__file__).resolve().parent
board = root/'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as folder:
    dll=Path(folder)/'voice.dll'
    subprocess.run([CC,'-shared','-std=c11','-Wall','-Wextra','-Werror',
                    str(board/'nabo_voice_commands.c'),'-o',str(dll)],check=True)
    lib=ctypes.CDLL(str(dll))
    parse=lib.nabo_voice_launch_app
    parse.argtypes=[ctypes.c_char_p];parse.restype=ctypes.c_char_p
    aliases={'牌桌':'table','大话骰':'table','二十一点':'table','21点':'table','打牌':'table','跳一跳':'jump',
             '设置':'settings','更多设置':'settings','工具':'tools',
             '谁是卧底':'undercover','卧底':'undercover','对决':'duel','联机对决':'duel','快枪手':'duel'}
    count=0
    for spoken,app in aliases.items():
        for template in ['打开{}。','请打开{}','小智，请你帮我打开{}吧！','我想玩{}','开启一下{}程序', ' 打开 {} 一下 ']:
            command=template.format(spoken)
            assert parse(command.encode())==app.encode(),command
            count+=1
    for command in ['', '牌桌','不要打开牌桌','别打开大话骰','我不想打开牌桌','如何打开牌桌',
                    '教我怎么打开大话骰','打开网络电台','打开牌桌有什么用','打开牌桌和节奏','打开牌桌然后关机',
                    '打开未知程序','打开电脑设置','我听过牌桌这个词','打开木鱼','打开深潜','打开节奏回声','不要打开对决','怎么打开对决','打开对决和牌桌','打开'+'牌桌'*100]:
        assert parse(command.encode()) is None,command
        count+=1
    assert parse(None) is None
    unload_library(lib._handle)
print(f'PASS: {count+1} production speech parser cases; negation/questions/ambiguity rejected')
