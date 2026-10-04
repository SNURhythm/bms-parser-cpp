// Consumer-contract probe for beatoraja ad42f56c4658e968f93b24bf23440fe51cb9878e.
// Compiles its actual SongInformation; evaluates LaneRenderer's visibility
// predicate without initializing graphics. See the accompanying audit for commands.
import bms.model.*;
import bms.player.beatoraja.song.SongInformation;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.logging.*;
public class ProbeBeatorajaLongNotes {
 static String inspect(String name,String body) {
  var model=new BMSDecoder().decode(("#BPM 120\n#WAV01 a.wav\n#WAV02 b.wav\n"+body).getBytes(StandardCharsets.UTF_8),name+".bms",false,new int[]{1});
  if(model==null)throw new AssertionError(name+": unexpected decoder rejection");
  BMSModelUtils.setStartNoteTime(model,1000);
  var active=Collections.newSetFromMap(new IdentityHashMap<Note,Boolean>());
  for(var tl:model.getAllTimeLines())for(int lane=0;lane<model.getMode().key;lane++)if(tl.getNote(lane)!=null)active.add(tl.getNote(lane));
  int heads=0,tails=0,drawCandidates=0,detached=0,nulls=0;String renderResult="ok";
  for(var note:active)if(note instanceof LongNote ln){
   if(ln.isEnd())tails++;else heads++;
   if(ln.getPair()==null)nulls++;else if(!active.contains(ln.getPair()))detached++;
   try {
    // Exact visibility condition from LaneRenderer.java:552. This probe
    // evaluates the predicate; it does not initialize graphics or draw pixels.
    if(!ln.isEnd() && ln.getPair().getMicroTime()>=0)drawCandidates++;
   }catch(NullPointerException ex){renderResult=ex.getClass().getSimpleName();}
  }
  String infoResult="ok";
  try{new SongInformation(model);}catch(RuntimeException ex){infoResult=ex.getClass().getSimpleName();}
  long normals=active.stream().filter(note -> note instanceof NormalNote).count();
  System.out.printf("%s normalNotes=%d activeHeads=%d activeTails=%d detachedReferences=%d nullPartners=%d drawCandidates=%d predicate=%s SongInformation=%s%n",name,normals,heads,tails,detached,nulls,drawCandidates,renderResult,infoResult);
  return heads+","+tails+","+detached+","+nulls+","+drawCandidates+","+renderResult+","+infoResult+","+model.getTotalNotes();
 }
 static void expect(String name,String body,String expected) {
  String actual=inspect(name,body);
  if(!expected.equals(actual))throw new AssertionError(name+": expected "+expected+", got "+actual);
 }
 static void verifyPreparedClassicTail(boolean earlyNote) {
  String body=(earlyNote?"#00012:01\n":"")+"#00151:0101\n#00111:0002\n";
  var model=new BMSDecoder().decode(("#BPM 120\n"+body).getBytes(StandardCharsets.UTF_8),"prep.bms",false,new int[]{1});
  LongNote head=null;
  for(var tl:model.getAllTimeLines())for(int lane=0;lane<model.getMode().key;lane++)
   if(tl.getNote(lane) instanceof LongNote ln && !ln.isEnd())head=ln;
  if(head==null || head.getPair()==null)throw new AssertionError("missing prepared fixture pair");
  long margin=BMSModelUtils.setStartNoteTime(model,1000);
  boolean positiveHold=head.getMicroTime()<head.getPair().getMicroTime()
      && head.getSection()<head.getPair().getSection();
  if(margin!=(earlyNote?1000:0) || positiveHold==earlyNote)
   throw new AssertionError("unexpected detached-tail start-time adjustment");
  System.out.println("prepared_classic earlyNote="+earlyNote+" marginMs="+margin+" positiveHold="+positiveHold);
 }
 public static void main(String[]args){Logger.getLogger("").setLevel(Level.OFF);
  verifyPreparedClassicTail(false);
  verifyPreparedClassicTail(true);
  expect("valid","#00151:0101\n","1,1,0,0,1,ok,ok,1");
  expect("detached_tail","#00151:0101\n#00111:0002\n","1,0,1,0,1,ok,ok,2");
  expect("detached_head","#00151:0101\n#00111:02\n","0,1,1,0,0,ok,ok,1");
  expect("unclosed_ordinary","#00151:01\n","0,0,0,0,0,ok,ok,0");
  expect("lnobj_without_end","#LNOBJ 02\n#00111:01\n","0,0,0,0,0,ok,ok,1");
  expect("lnobj_without_start","#LNOBJ 02\n#00111:02\n","0,0,0,0,0,ok,ok,0");
  for(int mode:new int[]{2,3}) {
   String prefix="#LNMODE "+mode+"\n";
   expect("mode"+mode+"_valid",prefix+"#00151:0101\n","1,1,0,0,1,ok,ok,2");
   expect("mode"+mode+"_detached_tail",prefix+"#00151:0101\n#00111:0002\n","1,0,1,0,1,ok,ok,2");
   expect("mode"+mode+"_detached_head",prefix+"#00151:0101\n#00111:02\n","0,1,1,0,0,ok,ok,2");
   expect("mode"+mode+"_null_sentinel",prefix+"#00002:5e-324\n#00151:01\n","1,0,0,1,0,NullPointerException,NullPointerException,1");
  }
  expect("null_sentinel","#00002:5e-324\n#00151:01\n","1,0,0,1,0,NullPointerException,NullPointerException,1");
 }
}
