import java.io.*;
import java.nio.charset.StandardCharsets;

// BMSDecoder in jbms-parser uses InputStreamReader(..., "MS932").
// Records are a big-endian 32-bit length followed by the encoded bytes.
public class MS932Oracle {
    public static void main(String[] args) throws Exception {
        var input = new DataInputStream(System.in);
        var output = new DataOutputStream(System.out);
        while (true) {
            int length;
            try { length = input.readInt(); }
            catch (EOFException end) { break; }
            byte[] bytes = input.readNBytes(length);
            if (bytes.length != length) throw new EOFException("Truncated record");
            var text = new StringBuilder();
            try (var reader = new InputStreamReader(new ByteArrayInputStream(bytes), "MS932")) {
                char[] buffer = new char[1024];
                int count;
                while ((count = reader.read(buffer)) != -1) text.append(buffer, 0, count);
            }
            byte[] utf8 = text.toString().getBytes(StandardCharsets.UTF_8);
            output.writeInt(utf8.length);
            output.write(utf8);
        }
        output.flush();
    }
}
