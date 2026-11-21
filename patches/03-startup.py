"""Start straight in the Calculation screen: no KhiCAS documentation QR page.
(Overclocking was only offered there; history is cleared from F6.)"""
from patchlib import patch

patch('main.cc', [
('''  if (1 ||
      calculator==1
      // && ram_filename[13]!='5'
      ){
    int key=QRdisp(''',
'''  if (0){ // Monolith: no QR start page
    int key=QRdisp('''),
], 'no QR start page')
print('03-startup: applied')
