"""Catppuccin Mocha colours for KhiCAS's own screens (graphs, catalogue, file
menu, editor): src/mocha.c recolours every frame, linked in with --wrap."""
from patchlib import patch

# QR codes stay black on white: phone cameras need dark modules on light
patch('console.cc', [
('''static int do_QRdisp(const uint8_t qrcode[],const char * msg1,const char * msg2,const char * msg3,const char *msg4) {
  drawRectangle(0,0,LCD_WIDTH_PX,LCD_HEIGHT_PX,0xffff);''',
'''extern "C" int mocha_paused;
static int do_QRdisp(const uint8_t qrcode[],const char * msg1,const char * msg2,const char * msg3,const char *msg4) {
  mocha_paused=1;
  drawRectangle(0,0,LCD_WIDTH_PX,LCD_HEIGHT_PX,0xffff);'''),
('''    if (key==KEY_CTRL_OK || key==KEY_CTRL_EXIT || key==KEY_CTRL_DEL || key==KEY_CHAR_EXP){
      EnableStatusArea(0);
      return key;''',
'''    if (key==KEY_CTRL_OK || key==KEY_CTRL_EXIT || key==KEY_CTRL_DEL || key==KEY_CHAR_EXP){
      EnableStatusArea(0);
      mocha_paused=0;
      return key;'''),
], 'mocha_paused')

patch('Makefile', [
('LDFLAGS =  -g -static -nostdlib -Tprizm.ld -Wl,--gc-sections,--print-memory-usage',
 'MOCHA_WRAP = -Wl,--wrap=Bdisp_PutDisp_DD,--wrap=GetKey,--wrap=GetKeyWait_OS,--wrap=PrintMini,--wrap=PrintXY\n'
 'LDFLAGS =  -g -static -nostdlib -Tprizm.ld -Wl,--gc-sections,--print-memory-usage $(MOCHA_WRAP)'),
('zdisplay.o console.o main.o\n', 'zdisplay.o console.o main.o mocha.o\n'),
('qrcodegen.o: qrcodegen.c', 'mocha.o: mocha.c mocha_tables.h\n\t$(CC) -std=gnu99 $(CFLAGS) -c $<\n\nqrcodegen.o: qrcodegen.c'),
], 'MOCHA_WRAP')
print('01-mocha: applied')
