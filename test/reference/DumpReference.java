import bms.model.*;
import java.nio.file.*;
import java.util.*;
import java.util.logging.*;

public class DumpReference {
    private static String quote(String s) {
        return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    private static String wav(BMSModel model, int id) {
        return quote(id < 0 ? "-" : model.getWavList()[id]);
    }

    private static String bmp(BMSModel model, int id) {
        return quote(id < 0 ? "-" : model.getBgaList()[id]);
    }

    private static int lane(BMSModel model, int key) {
        if (model.getMode() == Mode.BEAT_5K || model.getMode() == Mode.BEAT_10K)
            return key < 5 ? key : key == 5 ? 7 : key < 11 ? key + 2 : 15;
        return key;
    }

    private static int mode(BMSModel model) {
        return model.getMode() == Mode.BEAT_5K ? 5 :
               model.getMode() == Mode.BEAT_7K ? 7 :
               model.getMode() == Mode.BEAT_10K ? 10 :
               model.getMode() == Mode.BEAT_14K ? 14 : 9;
    }

    public static void main(String[] args) throws Exception {
        Logger.getLogger("").setLevel(Level.OFF);
        BMSDecoder decoder = new BMSDecoder();
        int[] randoms = new int[10000];
        String[] choices = System.getenv().getOrDefault("BMS_REFERENCE_RANDOM_VALUES", "1").split(",");
        for (int i = 0; i < randoms.length; i++) randoms[i] = Integer.parseInt(choices[i % choices.length]);
        for (String name : args) {
            Path path = Path.of(name);
            BMSModel model = decoder.decode(Files.readAllBytes(path), name,
                    name.toLowerCase(Locale.ROOT).endsWith(".pms"), randoms);
            System.out.println("FILE " + path.getFileName());
            if (model == null) {
                System.out.println("NULL");
                continue;
            }
            System.out.println("META notes=" + model.getTotalNotes() + " mode=" + mode(model)
                    + " difficulty=" + model.getDifficulty() + " bpm=" + model.getBpm()
                    + " min=" + model.getMinBPM() + " max=" + model.getMaxBPM()
                    + " player=" + model.getPlayer() + " lnmode=" + model.getLnmode()
                    + " total=" + model.getTotal() + " rank=" + model.getJudgerank()
                    + " hastotal=" + (model.getTotalType() == BMSModel.TotalType.BMS ? 1 : 0)
                    + " volwav=" + model.getVolwav()
                    + " ranktype=" + model.getJudgerankType().ordinal());
            System.out.println("TEXT title=" + quote(model.getTitle())
                    + " subtitle=" + quote(model.getSubTitle())
                    + " genre=" + quote(model.getGenre())
                    + " artist=" + quote(model.getArtist())
                    + " subartist=" + quote(model.getSubArtist())
                    + " playlevel=" + quote(model.getPlaylevel())
                    + " stage=" + quote(model.getStagefile())
                    + " banner=" + quote(model.getBanner())
                    + " back=" + quote(model.getBackbmp())
                    + " preview=" + quote(model.getPreview()));
            for (var entry : new TreeMap<>(model.getValues()).entrySet())
                System.out.println("VALUE key=" + quote(entry.getKey()) + " value=" + quote(entry.getValue()));
            Set<Note> attached = Collections.newSetFromMap(new IdentityHashMap<>());
            for (TimeLine tl : model.getAllTimeLines())
                for (int key = 0; key < model.getMode().key; key++)
                    if (tl.getNote(key) != null) attached.add(tl.getNote(key));
            for (TimeLine tl : model.getAllTimeLines()) {
                System.out.println("TL pos=" + tl.getSection() + " time=" + tl.getMicroTime()
                        + " bpm=" + tl.getBPM() + " scroll=" + tl.getScroll()
                        + " stop=" + tl.getMicroStop());
                if (tl.getBGA() != -1 || tl.getLayer() != -1)
                    System.out.println("BGA base=" + bmp(model, tl.getBGA())
                            + " layer=" + bmp(model, tl.getLayer()));
                for (Layer layer : tl.getEventlayer()) {
                    for (Layer.Sequence[] sequence : layer.sequence) {
                        for (int frame = 0; frame < sequence.length; frame++)
                            if (!sequence[frame].isEnd())
                                System.out.println("POOR frame=" + frame + " wav=" + bmp(model, sequence[frame].id));
                    }
                }
                for (int key = 0; key < model.getMode().key; key++) {
                    Note note = tl.getNote(key);
                    if (note != null) {
                        System.out.print("NOTE lane=" + lane(model, key) + " kind="
                                + note.getClass().getSimpleName() + " wav=" + wav(model, note.getWav()));
                        if (note instanceof MineNote)
                            System.out.print(" damage=" + ((MineNote) note).getDamage());
                        if (note instanceof LongNote) {
                            LongNote ln = (LongNote) note;
                            System.out.print(" pair=" + (ln.getPair() == null ? "null" : ln.getPair().getSection())
                                    + " type=" + ln.getType() + " end=" + (ln.isEnd() ? 1 : 0));
                            LongNote pair = ln.getPair();
                            if (pair != null) {
                                if (pair.getPair() != ln) throw new AssertionError("invalid LN pair");
                                System.out.print(" pairwav=" + wav(model, pair.getWav())
                                        + " pairtype=" + pair.getType() + " pairend=" + (pair.isEnd() ? 1 : 0)
                                        + " pairtime=" + pair.getMicroTime() + " pairattached=" + (attached.contains(pair) ? 1 : 0));
                            }
                        }
                        System.out.println();
                    }
                    Note hidden = tl.getHiddenNote(key);
                    if (hidden != null)
                        System.out.println("HIDDEN lane=" + lane(model, key) + " wav=" + wav(model, hidden.getWav()));
                }
                for (Note note : tl.getBackGroundNotes())
                    System.out.println("BG wav=" + wav(model, note.getWav()));
            }
        }
    }
}
