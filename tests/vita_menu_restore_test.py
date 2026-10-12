"""Regression checks for restoring Vita Settings after mod menu replacement."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'src/menu/menudef.cpp').read_text()
start = source.index('static void M_EnsureVitaSettings(')
end = source.index('\n#endif', start)
code = r'''
#include <cassert>
#include <string>
#include <vector>
using FName = std::string;
template<class T> struct Items : std::vector<T> {
 unsigned Size() const { return this->size(); }
 void Push(T item) { this->push_back(item); }
};
enum { MDESC_OptionsMenu, MDESC_ListMenu, MDESC_Other };
struct FMenuDescriptor { int mType; };
struct FListMenuItem {
 FName action; int y;
 FListMenuItem(FName a, int pos=0):action(a),y(pos){}
 virtual ~FListMenuItem(){}
 FName GetAction(int*) { return action; }
 int GetY() { return y; }
};
struct FOptionMenuItemSubmenu : FListMenuItem {
 FOptionMenuItemSubmenu(const char*,const char* a):FListMenuItem(a){}
};
struct FListMenuItemText : FListMenuItem {
 FListMenuItemText(int,int y,int,int,const char*,void*,int,int,FName a)
 :FListMenuItem(a,y){}
};
struct FOptionMenuDescriptor : FMenuDescriptor {
 Items<FListMenuItem*> mItems;
 FOptionMenuDescriptor(){mType=MDESC_OptionsMenu;}
 ~FOptionMenuDescriptor(){for(auto p:mItems)delete p;}
};
struct FListMenuDescriptor : FMenuDescriptor {
 Items<FListMenuItem*> mItems;
 int mXpos=20,mYpos=10,mLinespacing=12,mFontColor=1,mFontColor2=2;
 void* mFont=nullptr;
 FListMenuDescriptor(){mType=MDESC_ListMenu;}
 ~FListMenuDescriptor(){for(auto p:mItems)delete p;}
};
'''
code += source[start:end] + r'''
int main(){
 // A mod replaces the stock menu and supplies its own controls.
 FOptionMenuDescriptor mod;
 auto custom=new FListMenuItem("BrutalOptions"); mod.mItems.Push(custom);
 M_EnsureVitaSettings(&mod);
 assert(mod.mItems.Size()==2 && mod.mItems[0]==custom);
 assert(mod.mItems[1]->GetAction(nullptr)=="VitaSettings");
 // Stock menus and repeated creation must not accumulate duplicate entries.
 M_EnsureVitaSettings(&mod); assert(mod.mItems.Size()==2);
 FOptionMenuDescriptor empty; M_EnsureVitaSettings(&empty);
 assert(empty.mItems.Size()==1);
 // List-menu replacement keeps existing items and positions below their bottom.
 FListMenuDescriptor list;
 list.mItems.Push(new FListMenuItem("ModOptions",80));
 M_EnsureVitaSettings(&list);
 assert(list.mItems.Size()==2 && list.mItems[1]->GetY()==92);
 assert(list.mYpos==104);
 M_EnsureVitaSettings(&list); assert(list.mItems.Size()==2);
 FMenuDescriptor other; other.mType=MDESC_Other;
 M_EnsureVitaSettings(&other);
}
'''
# Ensure the repair runs during menu finalization after MENUDEF parsing.
create = source[source.index('void M_CreateMenus()'):source.index('void M_StartupSkillMenu')]
assert 'M_EnsureVitaSettings(*options)' in create
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp); (p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined',
                 str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Vita menu restoration tests passed')
