/* Coffee GB regression/captures for Atari-derived level colours.
 *
 * java --class-path "$COLOR_TEST_CP" tools/ColorTest.java ROM.gbc ROM.noi \
 *     ATARI_SOURCE_ROOT PALETTE.txt OUTPUT_DIRECTORY [nearest|legacy]
 * Requires the matching render.sym beside ROM.noi. Default: nearest.
 * Legacy checks the previous palette encoding and reports glyph mismatches;
 * nearest asserts corrected palettes, HUD, and all 56 boards' attributes.
 * Captures are native 160x144 frames without LCD colour correction. Gameplay
 * Robbo uses background tiles, so its normal BG attribute is checked too.
 */
import eu.rekawek.coffeegb.core.Gameboy;
import eu.rekawek.coffeegb.core.GameboyType;
import eu.rekawek.coffeegb.core.cpu.Cpu;
import eu.rekawek.coffeegb.core.events.EventBus;
import eu.rekawek.coffeegb.core.events.EventBusImpl;
import eu.rekawek.coffeegb.core.gpu.Display;
import eu.rekawek.coffeegb.core.joypad.Button;
import eu.rekawek.coffeegb.core.joypad.ButtonPressEvent;
import eu.rekawek.coffeegb.core.joypad.ButtonReleaseEvent;
import eu.rekawek.coffeegb.core.serial.SerialEndpoint;
import java.awt.Color;
import java.awt.Font;
import java.awt.image.BufferedImage;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.function.BooleanSupplier;
import java.util.regex.Pattern;
import javax.imageio.ImageIO;

@SuppressWarnings("deprecation")
public class ColorTest implements AutoCloseable {
    private static final int WIDTH = 16, HEIGHT = 31, CELL_BYTES = 14;
    private final EventBus bus = new EventBusImpl();
    private final Gameboy gb;
    private final Map<String, Integer> symbols;
    private final Map<Integer, int[]> palette = new HashMap<>();
    private final List<Level> levels = new ArrayList<>();
    private final int[] look = new int[128], pixels = new int[160 * 144];
    private final int board, robbo, currentBank, update, slotOwner;
    private final boolean legacy;
    private final Path output;
    private final Set<Integer> checked = new HashSet<>();
    private final List<String> mismatches = new ArrayList<>();
    private long ticks;
    private int frames;
    private record Level(List<String> rows, byte[] metadata) { }

    private ColorTest(Path rom, Path noi, Path source, Path colors, Path output,
                      boolean legacy) throws Exception {
        this.output = output;
        this.legacy = legacy;
        symbols = symbols(noi, "^DEF\\s+(\\S+)\\s+0x([0-9a-fA-F]+)$");
        var locals = symbols(noi.resolveSibling("render.sym"),
                "^\\s*\\d+\\s+(\\S+)\\s+([0-9a-fA-F]{8})\\s+[A-Z]+\\s*$");
        board = symbol("_board");
        robbo = symbol("_robbo");
        currentBank = symbol("__current_bank");
        update = symbol("_update_game");
        slotOwner = symbol("_gr_logo_rainbow") - locals.get("_gr_logo_rainbow") + locals.get("_slot_owner");
        for (String name : List.of("C1", "C2", "C3")) {
            List<String> lines = Files.readAllLines(source.resolve("d2/" + name + ".txt"));
            List<String> rows = null;
            for (int i = 0; i < lines.size(); i++) {
                String line = lines.get(i);
                if (line.equals("### level ###")) rows = List.copyOf(lines.subList(i + 1, i + 32));
                if (line.startsWith("metadata:")) {
                    byte[] metadata = java.util.HexFormat.of().parseHex(line.substring(9).trim());
                    check(rows != null && metadata.length == 16, "invalid source level");
                    levels.add(new Level(rows, metadata));
                    rows = null;
                }
            }
        }
        check(levels.size() == 56, "expected 56 Atari levels");
        for (String line : Files.readAllLines(colors)) {
            line = line.strip();
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] fields = line.split("\\s+");
            palette.put(Integer.parseInt(fields[0], 16), new int[] {
                    Integer.parseInt(fields[1]), Integer.parseInt(fields[2]), Integer.parseInt(fields[3])});
        }
        boolean readingLook = false;
        int p = 0;
        var lookValue = Pattern.compile("\\s*DTA B\\(\\$([0-9a-fA-F]+)\\).*", Pattern.CASE_INSENSITIVE);
        for (String line : Files.readAllLines(source.resolve("d1/R2.ASM"))) {
            if (line.startsWith("LOOK EQU")) readingLook = true;
            if (readingLook && p < 128) {
                var match = lookValue.matcher(line);
                if (match.matches()) look[p++] = Integer.parseInt(match.group(1), 16);
            }
        }
        check(p == 128, "could not parse Atari LOOK table");
        gb = new Gameboy.GameboyConfiguration(rom.toFile())
                .setBootstrapMode(Gameboy.BootstrapMode.SKIP)
                .setGameboyType(GameboyType.CGB).setSupportBatterySave(false).build();
        gb.init(bus, SerialEndpoint.NULL_ENDPOINT, null);
        bus.register((Display.GbcFrameReadyEvent event) -> {
            frames++;
            event.toRgb(pixels, false);
        }, Display.GbcFrameReadyEvent.class);
    }
    private static Map<String, Integer> symbols(Path file, String regex) throws Exception {
        Map<String, Integer> result = new HashMap<>();
        var pattern = Pattern.compile(regex);
        for (String line : Files.readAllLines(file)) {
            var match = pattern.matcher(line);
            if (match.matches()) result.put(match.group(1), Integer.parseInt(match.group(2), 16));
        }
        return result;
    }
    private int symbol(String name) {
        Integer value = symbols.get(name);
        if (value == null) throw new IllegalArgumentException("Missing symbol " + name);
        return value;
    }
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    private int b(int address) { return gb.getAddressSpace().getByte(address) & 255; }
    private int w(int address) { return b(address) | b(address + 1) << 8; }
    private void put(int address, int value) { gb.getAddressSpace().setByte(address, value & 255); }
    private void putWord(int address, int value) { put(address, value); put(address + 1, value >> 8); }
    private boolean at(int address) {
        return gb.getCpu().getState() == Cpu.State.OPCODE
                && gb.getCpu().getRegisters().getPC() == (address & 65535)
                && (address < 0x4000 || b(currentBank) == address >> 16);
    }
    private void tick() { gb.tick(); ticks++; }
    private void until(BooleanSupplier condition, String description) {
        long limit = ticks + 30_000_000;
        while (!condition.getAsBoolean()) {
            check(ticks < limit, "timeout: " + description);
            tick();
        }
    }
    private void next(int address) {
        until(() -> !at(address), "leave entry");
        until(() -> at(address), "function " + Integer.toHexString(address));
    }
    private void runFrames(int count) {
        int end = frames + count;
        until(() -> frames >= end, "display frames");
    }
    private void runTicks(int count) { gb.runTicks(count); ticks += count; }
    private void boot() {
        runTicks(5_000_000);
        bus.post(new ButtonPressEvent(Button.START));
        runTicks(1_200_000);
        bus.post(new ButtonReleaseEvent(Button.START));
        next(update);
    }
    private int color(int code) {
        // GTIA ignores hue/luminance bit 0. The old partial fixture also kept
        // a few odd aliases, so legacy mode permits those exact keys.
        int key = !legacy || !palette.containsKey(code) ? code & 254 : code;
        int[] rgb = palette.get(key);
        check(rgb != null, "palette lacks Atari colour " + Integer.toHexString(key));
        int result = 0;
        for (int i = 0; i < 3; i++) {
            int component = legacy ? rgb[i] >> 3 : (rgb[i] * 31 + 127) / 255;
            result |= component << (i * 5);
        }
        return result;
    }
    private void palettes(int levelNumber) {
        byte[] metadata = levels.get(levelNumber - 1).metadata;
        int[] expectedIndices = {7, 2, 3, 4, 7, 2, 3, 5};
        byte[] actual = gb.getGpu().captureBessBackgroundPalettes();
        for (int i = 0; i < 12; i++) {
            int index = i < 8 ? expectedIndices[i] : legacy ? 6 : 2;
            int expected = color(metadata[index] & 255);
            int value = (actual[i * 2] & 255) | (actual[i * 2 + 1] & 255) << 8;
            check(value == expected, String.format("L%d palette %d[%d]: got %04X expected %04X",
                    levelNumber, i / 4, i % 4, value, expected));
        }
        int[] hud = {0, 0x7FFF, 0x167A, 0x7FFF};
        if (!legacy) {
            int bg = color(metadata[6] & 255), text = color((metadata[6] & 0xF0) | 0x0A);
            hud = new int[] {bg, bg, bg, text};
        }
        for (int i = 0; i < 4; i++) {
            int value = (actual[48 + i * 2] & 255) | (actual[49 + i * 2] & 255) << 8;
            check(value == hud[i], "L" + levelNumber + " HUD palette entry " + i);
        }
    }
    private static int atariByte(char c) {
        return switch (c) {
            case '♥' -> 0x00; case '├' -> 0x01; case '┤' -> 0x04; case '┐' -> 0x05;
            case '╱' -> 0x06; case '◣' -> 0x0A; case '▔' -> 0x0D; case '▂' -> 0x0E;
            case '▖' -> 0x0F; case '┌' -> 0x11; case '─' -> 0x12; case '┼' -> 0x13;
            case '•' -> 0x14; case '┬' -> 0x17; case '┴' -> 0x18; case '↑' -> 0x1C;
            case '↓' -> 0x1D; case '←' -> 0x1E; case '→' -> 0x1F; case '█' -> 0xA0;
            default -> c;
        };
    }
    private void attributes(int levelNumber) {
        Level source = levels.get(levelNumber - 1);
        var attrs = gb.getGpu().getVideoRam1();
        int tileMap = (b(0xFF40) & 8) != 0 ? 0x9C00 : 0x9800;
        for (int slot = 0; slot < 16; slot++) {
            int y = b(slotOwner + slot);
            if (y >= HEIGHT) continue;
            for (int x = 0; x < WIDTH; x++) {
                int id = (levelNumber - 1) * WIDTH * HEIGHT + y * WIDTH + x;
                if (!checked.add(id)) continue;
                int glyph = atariByte(source.rows.get(y).charAt(x));
                int expected = glyph == 0x13 ? 2 : glyph == 0xA0 ? 1 : look[glyph & 127] >> 7;
                for (int q = 0; q < 4; q++) {
                    int address = tileMap + slot * 64 + x * 2 + (q & 1) + (q >> 1) * 32;
                    int actual = attrs.getByte(address) & 7;
                    if (actual != expected) mismatches.add(String.format(
                            "%d\t%d\t%d\t%02X\t%d\t%d\t%d", levelNumber, x, y, glyph, q, expected, actual));
                }
            }
        }
    }
    private BufferedImage capture(int number) throws Exception {
        BufferedImage image = new BufferedImage(160, 144, BufferedImage.TYPE_INT_RGB);
        image.setRGB(0, 0, 160, 144, pixels, 0, 160);
        ImageIO.write(image, "png", output.resolve(String.format("level-%02d.png", number)).toFile());
        return image;
    }
    private void freeze() {
        // Stop actors before the pending first update, then let the production
        // renderer/camera keep running. Only scheduling flags are changed.
        for (int i = 0; i < WIDTH * HEIGHT; i++) {
            int flags = board + i * CELL_BYTES + 13;
            put(flags, b(flags) & ~4);
        }
        for (int y = 0; y < HEIGHT; y++) put(symbol("_gr_row_active") + y, 0);
        putWord(symbol("_game_mode"), 4);
        // Finish that one pending call at the original safe spawn before
        // moving the camera target onto arbitrary parts of the frozen map.
        runFrames(2);
    }
    private void flash() {
        byte[] original = gb.getGpu().captureBessBackgroundPalettes();
        int[] normal = gb.getGpu().getBgPalette().getPalette(0);
        int[] inverse = gb.getGpu().getBgPalette().getPalette(1);
        int floor = normal[0], white = color(0x0F);
        check(floor != white, "flash test requires a non-white floor");
        // Exercise the same presentation request made by open_exit(), without
        // changing the solved state of a level or advancing any actors.
        putWord(symbol("_level") + 4, 1);
        until(() -> normal[0] == white && inverse[0] == white, "exit flash");
        int started = w(symbol("_sys_time"));
        byte[] lit = gb.getGpu().captureBessBackgroundPalettes();
        for (int i = 0; i < original.length; i++)
            if (i != 0 && i != 1 && i != 8 && i != 9)
                check(lit[i] == original[i], "exit flash modified palette byte " + i);
        until(() -> normal[0] == floor && inverse[0] == floor, "exit flash restoration");
        int duration = w(symbol("_sys_time")) - started & 65535;
        check(duration == 4, "exit flash lasted " + duration + " GBC frames, expected 4");
        check(java.util.Arrays.equals(original, gb.getGpu().captureBessBackgroundPalettes()),
                "exit flash did not restore every palette entry");
        check(w(symbol("_level") + 4) == 0, "exit flash request was not consumed");
        System.out.println("PASS exit flash changes only the two COLB entries for four VBlanks");
    }
    private void test() throws Exception {
        Files.createDirectories(output);
        BufferedImage sheet = new BufferedImage(160 * 8, 162 * 7, BufferedImage.TYPE_INT_RGB);
        var graphics = sheet.createGraphics();
        graphics.setFont(new Font(Font.MONOSPACED, Font.PLAIN, 12));
        boot();
        for (int n = 1; n <= 56; n++) {
            palettes(n);
            attributes(n);
            BufferedImage image = capture(n);
            int cx = (n - 1) % 8 * 160, cy = (n - 1) / 8 * 162;
            graphics.drawImage(image, cx, cy + 18, null);
            graphics.setColor(Color.WHITE);
            graphics.drawString("Level " + n, cx + 4, cy + 13);
            if (!legacy) {
                freeze();
                if (n == 1) flash();
                for (int y : new int[] {2, 15, 28}) {
                    putWord(robbo + 2, y);
                    runFrames(160);
                    attributes(n);
                }
            }
            if (n % 8 == 0) System.out.printf("Checked/captured levels 1..%d (%d cells, %d attribute differences)%n",
                    n, checked.size(), mismatches.size());
            if (n < 56) {
                putWord(symbol("_game_mode"), 1);
                putWord(symbol("_level_packs") + 4, n + 1);
                next(symbol("_level_init"));
                next(update);
            }
        }
        graphics.dispose();
        ImageIO.write(sheet, "png", output.resolve("contact-sheet.png").toFile());
        List<String> rows = new ArrayList<>();
        rows.add("level\tx\ty\tatari_glyph\ttile_quadrant\texpected_palette\tactual_palette");
        rows.addAll(mismatches);
        Files.write(output.resolve("attribute-differences.tsv"), rows);
        if (!legacy) {
            check(checked.size() == 56 * WIDTH * HEIGHT, "not every board cell was checked");
            check(mismatches.isEmpty(), mismatches.size() + " source glyph palette differences; see report");
        }
        System.out.printf("PASS all 56 runtime level/HUD palettes; %d source cells checked; %d attribute differences%n",
                checked.size(), mismatches.size());
        System.out.println("Captures: " + output.toAbsolutePath());
    }
    @Override public void close() { try { gb.close(); } finally { bus.close(); } }
    public static void main(String[] args) throws Exception {
        if (args.length < 5 || args.length > 6) throw new IllegalArgumentException(
                "Usage: ColorTest ROM.gbc ROM.noi ATARI_SOURCE_ROOT PALETTE.txt OUTPUT_DIRECTORY [nearest|legacy]");
        boolean legacy = args.length == 6 && args[5].equals("legacy");
        try (ColorTest test = new ColorTest(Path.of(args[0]), Path.of(args[1]), Path.of(args[2]),
                Path.of(args[3]), Path.of(args[4]), legacy)) { test.test(); }
    }
}
