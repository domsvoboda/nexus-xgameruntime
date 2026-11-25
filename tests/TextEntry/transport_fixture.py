# SPDX-License-Identifier: LGPL-2.1-or-later
import sys,os,socket,struct,importlib.util
from pathlib import Path
spec=importlib.util.spec_from_file_location('model',Path(__file__).with_name('text-entry-model.py'))
model=importlib.util.module_from_spec(spec);sys.modules[spec.name]=model;spec.loader.exec_module(model)
assert len(sys.argv)==3 and sys.argv[1]=='--port'
port=int(sys.argv[2]);token=bytes.fromhex(os.environ.pop('NEXUS_NATIVE_TEXT_TOKEN'));assert len(token)==32
# A wrong-token client must be disconnected before any game text is sent.
with socket.create_connection(('127.0.0.1',port),timeout=5) as wrong:
 wrong.sendall(bytes(32));assert wrong.recv(1)==b''
with socket.create_connection(('127.0.0.1',port),timeout=5) as channel:
 channel.settimeout(10);channel.sendall(token)
 with channel.makefile('rb') as reader,channel.makefile('wb') as writer:
  request=model.read_request(reader);case=int(request.title.split(':')[1])
  values={0:'café 東京 🏎',3:'abcdef',4:'',5:'12ab34.567890',6:'x'*1030,7:'🏎a',8:'ab12',9:'ab12',10:'abcde'}
  if case==2:
   assert reader.read(1)==b''
  elif case==1:model.write_result(writer,model.CANCELLED)
  else:
   if case==10:assert request.password and request.limit==1024
   model.write_result(writer,0,request.sanitize(values[case]))
