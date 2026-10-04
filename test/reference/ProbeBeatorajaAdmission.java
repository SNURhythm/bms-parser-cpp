// Uses unmodified beatoraja SongData, including the caller of SongInformation.
// This is a headless caller-boundary probe, not a full application launch.
import bms.model.*;
import bms.player.beatoraja.song.SongData;
import bms.player.beatoraja.song.SongInformationAccessor;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.logging.*;

public class ProbeBeatorajaAdmission {
  static BMSModel decode(String body) {
    var bytes = ("#BPM 120\n#WAV01 a.wav\n#WAV02 b.wav\n" + body)
        .getBytes(StandardCharsets.UTF_8);
    var model = new BMSDecoder().decode(bytes, "probe.bms", false, new int[]{1});
    if (model == null) throw new AssertionError("decoder rejected fixture");
    BMSModelUtils.setStartNoteTime(model, 1000);
    return model;
  }

  static void inspect(String name, String body, boolean failsFullInformation) {
    var model = decode(body);
    // SQLiteSongDatabaseAccessor uses the lightweight constructor at scan time.
    var scanned = new SongData(model, false, false);
    if (scanned.getBMSModel() != model || scanned.getInformation() != null)
      throw new AssertionError(name + ": lightweight scan changed the model");
    boolean constructorFailed = false, updateFailed = false;
    try { new SongData(model, false); }
    catch (NullPointerException expected) { constructorFailed = true; }
    // PlayerResource.setBMSFile calls this overload for a selected SongData.
    try { scanned.setBMSModel(model); }
    catch (NullPointerException expected) { updateFailed = true; }
    if (constructorFailed != failsFullInformation || updateFailed != failsFullInformation)
      throw new AssertionError(name + ": unexpected caller exception behavior");
    System.out.printf("%s lightweight=accepted fullConstructor=%s setBMSModel=%s%n",
        name, constructorFailed ? "throws NPE" : "accepted",
        updateFailed ? "throws NPE" : "accepted");
  }

  static void databaseContinues() throws Exception {
    var directory = Files.createTempDirectory("beatoraja-admission-");
    var path = directory.resolve("information.db");
    try {
      var database = new SongInformationAccessor(path.toString());
      if (!database.startUpdate()) throw new AssertionError("database transaction did not start");
      var malformed = decode("#00002:5e-324\n#00151:01\n");
      var valid = decode("#00151:0101\n");
      database.update(malformed);
      database.update(valid);
      database.endUpdate();
      if (database.getInformation(malformed.getSHA256()) != null ||
          database.getInformation(valid.getSHA256()) == null)
        throw new AssertionError("derived-information failure did not skip and continue");
      System.out.println("database update: malformed information skipped, next chart persisted");
    } finally {
      try (var files = Files.list(directory)) {
        for (var file : files.toList()) Files.deleteIfExists(file);
      }
      Files.deleteIfExists(directory);
    }
  }

  public static void main(String[] args) throws Exception {
    Logger.getLogger("").setLevel(Level.OFF);
    inspect("valid", "#00151:0101\n", false);
    inspect("detached_tail", "#00151:0101\n#00111:0002\n", false);
    inspect("detached_head", "#00151:0101\n#00111:02\n", false);
    for (int mode : new int[]{1, 2, 3})
      inspect("null_sentinel_mode" + mode,
          "#LNMODE " + mode + "\n#00002:5e-324\n#00151:01\n", true);
    // The bundled SQLite JDBC native library must support the host platform.
    if (java.util.Arrays.asList(args).contains("--database")) {
      Logger.getLogger("").setLevel(Level.WARNING);
      databaseContinues();
    }
  }
}
