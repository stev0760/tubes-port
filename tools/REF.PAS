program Ref;
{ Reference program for RTL identification. Touches as much of System, Crt
  and Dos as practical so that smart linking pulls those routines into the
  image, letting rtl_match.py locate them inside the target binary. }
uses Crt, Dos;

var
  s, t: string;
  c: char;
  tf: text;
  bf: file;
  r: real;
  i, j: integer;
  l: longint;
  p: pointer;
  y, m, d, dw: word;
  hh, mm, ss, s100: word;
  sr: SearchRec;
  reg: Registers;
  buf: array[0..127] of byte;
  n: word;

begin
  { Crt }
  ClrScr;
  GotoXY(1, 1);
  TextColor(7);
  TextBackground(0);
  Window(1, 1, 80, 25);
  ClrEol;
  DelLine;
  InsLine;
  HighVideo;
  LowVideo;
  NormVideo;
  Sound(440);
  Delay(10);
  NoSound;
  if KeyPressed then c := ReadKey;
  i := WhereX + WhereY;

  { System - string handling }
  s := 'abc';
  t := Concat(s, 'def');
  i := Pos('b', s);
  Delete(t, 1, 1);
  Insert('z', t, 1);
  t := Copy(s, 1, 2);
  i := Length(t);
  Val('123', l, j);
  Str(l, s);
  s := UpCase(c) + Chr(65);
  i := Ord(c);

  { System - arithmetic and floating point }
  r := Sqrt(2.0) + Sin(1.0) + Cos(1.0) + Ln(2.0) + Exp(1.0) + ArcTan(1.0);
  r := Frac(r) + Int(r) + Abs(r);
  i := Round(r);
  j := Trunc(r);
  l := Abs(-5);
  i := Sqr(3);
  Randomize;
  i := Random(100);
  r := Random;

  { System - heap }
  GetMem(p, 128);
  FreeMem(p, 128);
  New(p);
  Dispose(p);
  l := MemAvail + MaxAvail;

  { System - text and untyped file I/O }
  Assign(tf, 'OUT.TXT');
  Rewrite(tf);
  WriteLn(tf, s, i, r:0:2);
  Close(tf);
  Assign(tf, 'OUT.TXT');
  Reset(tf);
  if not Eof(tf) then ReadLn(tf, s);
  Close(tf);
  Erase(tf);

  Assign(bf, 'BIN.DAT');
  Rewrite(bf, 1);
  BlockWrite(bf, buf, SizeOf(buf), n);
  Seek(bf, 0);
  BlockRead(bf, buf, SizeOf(buf), n);
  l := FileSize(bf) + FilePos(bf);
  Close(bf);

  { System - memory moves }
  FillChar(buf, SizeOf(buf), 0);
  Move(buf[0], buf[64], 64);

  { Dos }
  GetDate(y, m, d, dw);
  GetTime(hh, mm, ss, s100);
  SetDate(y, m, d);
  SetTime(hh, mm, ss, s100);
  i := DosVersion;
  FindFirst('*.*', AnyFile, sr);
  FindNext(sr);
  GetIntVec($1C, p);
  SetIntVec($1C, p);
  Intr($21, reg);
  MsDos(reg);
  s := FSearch('X.TXT', '.');
  l := DiskFree(0) + DiskSize(0);

  WriteLn(s, i, l, r:0:4);
end.
