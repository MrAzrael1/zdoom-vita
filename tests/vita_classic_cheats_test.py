"""Check real classic-console handlers preserve cheat gates and network encoding."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/m_cheat.cpp').read_text()
start=source.index('static void cht_ConsoleDoomCheat')
end=source.index('\n#endif',start)
block=source[start:end]
names=re.findall(r'CCMD\((\w+)\)',block)
allsource='\n'.join(p.read_text(errors='replace') for p in (root/'src').rglob('*.cpp'))
for name in names+['iddqd','idclev','idmus']:
 assert len(re.findall(r'CCMD\s*\(\s*'+name+r'\s*\)',allsource))==1,name
constants=sorted(set(re.findall(r'\bCHT_\w+',block)))
code=r'''
#include <cassert>
#include <vector>
#include <cstdarg>
#include <cstdio>
static bool denied=false;static int checks=0;
bool CheckCheatmode(){++checks;return denied;}
enum{GS_LEVEL,GS_STARTUP};int gamestate=GS_LEVEL;
enum{DEM_GENERICCHEAT=99};
std::vector<int> wire;
void Net_WriteByte(int b){wire.push_back(b);}
void Printf(const char*,...){}
const char*GStrings(const char*){return "Power-up choices";}
#define CCMD(name) void Cmd_##name()
#define EXTERN_CVAR(type,name) int name=0;
'''
code+='enum{'+','.join(constants)+'};\n'+block+r'''
int main(){
 // Permission rejection must not alter player commands or automap state.
 denied=true;Cmd_idkfa();Cmd_idclip();Cmd_idbeholdv();Cmd_iddt();
 assert(wire.empty()&&am_cheat==0&&checks==4);
 // Title/startup state must not schedule cheats against absent game actors.
 denied=false;gamestate=GS_STARTUP;Cmd_idkfa();Cmd_idchoppers();Cmd_iddt();
 assert(wire.empty()&&am_cheat==0);
 // Cheats must enqueue the standard two-byte packet rather than mutate actors.
 gamestate=GS_LEVEL;Cmd_idkfa();
 assert((wire==std::vector<int>{DEM_GENERICCHEAT,CHT_IDKFA}));
 wire.clear();Cmd_idspispopd();Cmd_idclip();
 assert((wire==std::vector<int>{DEM_GENERICCHEAT,CHT_NOCLIP,DEM_GENERICCHEAT,CHT_NOCLIP}));
 wire.clear();Cmd_idbehold();assert(wire.empty());
 // The automap code cycles cleanly and honors the gate even mid-cycle.
 Cmd_iddt();assert(am_cheat==1);Cmd_iddt();assert(am_cheat==2);
 denied=true;Cmd_iddt();assert(am_cheat==2);
 denied=false;Cmd_iddt();assert(am_cheat==0);
 puts("Classic cheat console permissions, game-state, packets and automap tests passed");
}
'''
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined',
 str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
