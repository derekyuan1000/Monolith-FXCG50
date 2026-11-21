"""The Upsilon-style Calculation screen (src/zupsui.cpp, src/zupsmain.cc)
replaces KhiCAS's console, and the add-in becomes monolith.g3a + monolith.ac2."""
from patchlib import patch, append

patch('console.cc', [
# state shared with zupsmain.cc
('''int Console_GetKey(){
  int key;''',
'''int ups_active=0;              // Upsilon-style front end running (zupsmain.cc)
int ups_inject[4],ups_inject_n=0; // keys fed to Console_GetKey by the front end
int Console_GetKey(){
  int key;'''),
# keys injected by the front end instead of the keyboard (F5, F6, SETUP...)
('''    in_ckgetkey(&key,1,menu.c_str(),shiftmenu.c_str(),alphamenu.c_str(),menucolorbg);
    bool alph=oldalphastate;''',
'''    if (ups_inject_n>0)
      key=ups_inject[--ups_inject_n];
    else
      in_ckgetkey(&key,1,menu.c_str(),shiftmenu.c_str(),alphamenu.c_str(),menucolorbg);
    bool alph=oldalphastate;'''),
# the old console is not drawn while the front end runs
('''int Console_Disp(){
  bool minimini=false;''',
'''int Console_Disp(){
  if (ups_active)
    return CONSOLE_SUCCEEDED;
  bool minimini=false;'''),
], 'ups_active')

append('console.cc', '''
// F-key menu text, turned into the front end's toolbox
const char * ups_menu_config(){
  if (xcas_python_eval==1)
    return python_conf_standard;
  return original_cfg?(const char *)original_cfg:conf_standard;
}
''', 'ups_menu_config')

patch('main.cc', [
('''static volatile int menutimer=0;''',
'''int ups_translate_key(int code){ return translated_keys[code]; } // for zupsmain.cc
static volatile int menutimer=0;'''),
('''  unsigned char *expr=0;
  while(1){
    if((expr=Console_GetLine())==NULL) stop("memory error");''',
'''  unsigned char *expr=0;
  { void ups_main(); ups_main(); } // Upsilon-style front end, does not return
  while(1){
    if((expr=Console_GetLine())==NULL) stop("memory error");'''),
], 'ups_main')

# The RAM part is monolith.ac2, so Monolith and stock KhiCAS can both be installed
patch('catalogen.cpp', [
('const char ram_filename[]="\\\\\\\\fls0\\\\khicas50.8c2";',
 'const char ram_filename[]="\\\\\\\\fls0\\\\monolith.8c2"; // monolith.ac2'),
], 'monolith.8c2')

patch('Makefile', [
('zdisplay.o console.o main.o mocha.o\n', 'zdisplay.o console.o main.o mocha.o zupsui.o zupsmain.o\n'),
('''khicas50.ac2: khicasen.elf prizm.ld''',
'''# Monolith: the add-in and its RAM part
MONOLITH_VERSION ?= 0.1.0
monolith.ac2: khicasen.elf prizm.ld
	$(OBJCOPY) $(OBJFLAGS) -j .rominram khicasen.elf $@

monolith.g3a: khicasen.bin monolith-uns.png monolith-sel.png
	mkg3a -n basic:Monolith -n internal:MONOLITH -V $(MONOLITH_VERSION) -i uns:monolith-uns.png -i sel:monolith-sel.png $< $@

khicas50.ac2: khicasen.elf prizm.ld'''),
], 'monolith.ac2')
print('02-frontend: applied')
