using System;
using System.IO;
using System.Text;
using System.Linq;
using System.Collections.Generic;
using System.Xml;

class Lsb {
 static int I(byte[] b,int p){return checked((int)BitConverter.ToUInt32(b,p));}
 static ushort H(byte[] b,int p){return BitConverter.ToUInt16(b,p);}
 static void Put(byte[] b,int p,int v){Buffer.BlockCopy(BitConverter.GetBytes(v),0,b,p,4);}
 public static byte[] Merge(byte[] b,IEnumerable<XmlElement> delta){
  if(Encoding.ASCII.GetString(b,0,4)!="LSB2"||I(b,16)!=4)throw new Exception("Unsupported string table format");
  int start=I(b,12);int[] offsets=Enumerable.Range(0,4).Select(i=>I(b,20+4*i)).ToArray();var blocks=new byte[4][];
  // Some LOC tables carry an entire nested LSB2 as an opaque section.
  // Its leading bytes are a signature. Use section boundaries for the copy.
  for(int i=0;i<4;i++)if(offsets[i]!=0){int end=offsets.Where(o=>o>offsets[i]).DefaultIfEmpty(b.Length).Min();blocks[i]=new byte[end-offsets[i]];Buffer.BlockCopy(b,offsets[i],blocks[i],0,blocks[i].Length);}
  int n=I(b,start+4),pool=start+8+6*(n+1);var texts=new SortedDictionary<ushort,string>();
  for(int i=0;i<n;i++){int p=pool+2*I(b,start+10+6*i),e=p;while(H(b,e)!=0)e+=2;texts.Add(H(b,start+8+6*i),Encoding.Unicode.GetString(b,p,e-p));}
  var names=new Dictionary<ushort,string>();var order=new List<ushort>();
  if(offsets[1]!=0){int o=offsets[1],count=I(b,o+4),p=o+8+8*count;var ord=new SortedDictionary<int,ushort>();for(int i=0;i<count;i++){int r=o+8+8*i,k=H(b,r),at=p+I(b,r+4),e=at;while(b[e]!=0)e++;names[(ushort)k]=Encoding.ASCII.GetString(b,at,e-at);ord[H(b,r+2)]=(ushort)k;}order.AddRange(ord.Values);order.AddRange(names.Keys.Where(k=>!order.Contains(k)).OrderBy(k=>k));}
  if(delta.All(d=>{ushort k=ushort.Parse(d.GetAttribute("key"));return texts.ContainsKey(k)&&texts[k]==d.InnerText&&names.ContainsKey(k)&&names[k]==d.GetAttribute("name");}))return b;
  foreach(var d in delta){ushort k=ushort.Parse(d.GetAttribute("key"));string text=d.InnerText,name=d.GetAttribute("name");if(texts.ContainsKey(k)&&texts[k]!=text&&(!names.ContainsKey(k)||names[k]!=name))throw new Exception("String key conflict: "+k);texts[k]=text;if(!names.ContainsKey(k)&&name.Length>0){names[k]=name;order.Add(k);}}
  using(var ms=new MemoryStream())using(var w=new BinaryWriter(ms))using(var ps=new MemoryStream())using(var pw=new BinaryWriter(ps)){w.Write(0);w.Write(texts.Count);foreach(var kv in texts){w.Write(kv.Key);w.Write((int)ps.Position/2);pw.Write(Encoding.Unicode.GetBytes(kv.Value+"\0"));}w.Write((ushort)65535);w.Write((int)ps.Position/2);pw.Write((ushort)0);w.Write(ps.ToArray());blocks[0]=ms.ToArray();Put(blocks[0],0,blocks[0].Length);}
  if(offsets[1]!=0){using(var ms=new MemoryStream())using(var w=new BinaryWriter(ms))using(var ps=new MemoryStream())using(var pw=new BinaryWriter(ps)){w.Write(0);w.Write(names.Count);var pooloff=new Dictionary<ushort,int>();foreach(ushort k in order){pooloff[k]=(int)ps.Position;pw.Write(Encoding.ASCII.GetBytes(names[k]+"\0"));}foreach(ushort k in names.Keys.OrderBy(x=>x)){w.Write(k);w.Write((ushort)(order.IndexOf(k)+1));w.Write(pooloff[k]);}w.Write(ps.ToArray());blocks[1]=ms.ToArray();Put(blocks[1],0,blocks[1].Length);}
   using(var ms=new MemoryStream())using(var w=new BinaryWriter(ms)){w.Write(8+2*order.Count);w.Write(order.Count);foreach(ushort k in order)w.Write(k);blocks[3]=ms.ToArray();}}
  byte[] header=new byte[start];Buffer.BlockCopy(b,0,header,0,start);using(var ms=new MemoryStream()){ms.Write(header,0,header.Length);foreach(int i in Enumerable.Range(0,4).OrderBy(x=>offsets[x])){if(blocks[i]==null)continue;Put(header,20+4*i,(int)ms.Position);ms.Write(blocks[i],0,blocks[i].Length);}var result=ms.ToArray();Buffer.BlockCopy(header,0,result,0,header.Length);return result;}
 }
}


