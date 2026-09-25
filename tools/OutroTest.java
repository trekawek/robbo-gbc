/* Coffee GB regression for previewing the ending from the pause menu.
 *
 * java --class-path "$COFFEE_GB_CP" tools/OutroTest.java ROM.gbc ROM.noi [CAPTURE_DIR]
 * The linker symbols must be from the same ROM build. Input goes through the
 * production menu, and returning from the preview preserves the loaded game.
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
import java.awt.image.BufferedImage;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.function.BooleanSupplier;
import java.util.regex.Pattern;
import javax.imageio.ImageIO;

@SuppressWarnings("deprecation")
public class OutroTest implements AutoCloseable {
    private static final int WIDTH = 16, HEIGHT = 31, CELL_BYTES = 14;
    private static final int KEY = 7, LCDC = 0xff40, WY = 0xff4a, NR42 = 0xff21;
    private static final int WAVE_A = 24, WAVE_B = 28;
    private static final String[][] ENDING_PAGES = {
        {"", "", "", "WELL DONE!", "", "ROBBO HAS ESCAPED", "THE HOSTILE",
                "PLANETARY SYSTEM.", "", "THE PLANS STORED", "IN HIS MEMORY ARE",
                "OF GREAT VALUE TO", "EARTH!", "", "", "START TO CONTINUE", "", ""},
        {"", "", "YOU HAVE COMPLETED", "OUR FIRST GAME.", "IF YOU ENJOYED IT,",
                "LOOK OUT FOR OUR", "NEXT RELEASES.", "", "REMEMBER:",
                "THE BEST GAMES", "COME FROM", "AVALON!", "", "", "", "PRESS START", "", ""}
    };
    private final EventBus bus = new EventBusImpl();
    private final Gameboy gb;
    private final Map<String, Integer> symbols = new HashMap<>();
    private final Path captures;
    private final Set<String> captured = new HashSet<>();
    private final int board, robbo, pack, score, currentBank;
    private long ticks;
    private int frames;
    private boolean watchEnding;
    private final int[] waveTiles = new int[32], waveStarts = new int[32];
    private int wavePoses, endingStartFrame;
    private int textFirstFrame = -1, textLastChangeFrame, textMapChanges;
    private int textBorderChanges, lastTextHash, lastBorderHash;
    private int fadePaletteChanges, lastFadePaletteHash;
    private boolean sawTextMap, sawBorder, sawFadePalette, awaitTextPattern;
    private String pageCapturePrefix = "text";
    private boolean watchWipe, sawWipeWindow;
    private int lastWipeY = -1, wipeMovement;
    private String captureNext;

    private OutroTest(Path rom, Path noi, Path captures) throws Exception {
        this.captures = captures;
        if (captures != null) Files.createDirectories(captures);
        var pattern = Pattern.compile("^DEF\\s+(\\S+)\\s+0x([0-9a-fA-F]+)$");
        for (String line : Files.readAllLines(noi)) {
            var match = pattern.matcher(line);
            if (match.matches()) symbols.put(match.group(1), Integer.parseInt(match.group(2), 16));
        }
        board = symbol("_board");
        robbo = symbol("_robbo");
        pack = symbol("_level_packs");
        score = symbol("_gr_score");
        currentBank = symbol("__current_bank");
        gb = new Gameboy.GameboyConfiguration(rom.toFile())
                .setBootstrapMode(Gameboy.BootstrapMode.SKIP)
                .setGameboyType(GameboyType.CGB).setSupportBatterySave(false).build();
        gb.init(bus, SerialEndpoint.NULL_ENDPOINT, null);
        bus.register(this::onFrame,
                Display.GbcFrameReadyEvent.class);
    }

    private void onFrame(Display.GbcFrameReadyEvent event) {
        frames++;
        if (captureNext != null) {
            capture(event, captureNext);
            captureNext = null;
        }
        if (watchWipe && (b(LCDC) & 0x20) != 0) {
            int wy = b(WY);
            if (lastWipeY >= 0 && wy < lastWipeY) wipeMovement++;
            sawWipeWindow = true;
            lastWipeY = wy;
            if (wipeMovement == 60) capture(event, "text-wipe");
        }
        if (!watchEnding) return;
        var oam = gb.getGpu().captureDebugGraphicsInspection().oam();
        int y = oam.unsignedByteAt(0), x = oam.unsignedByteAt(1);
        int tile = oam.unsignedByteAt(2);
        if (y == 128 && x == 116 && (tile == WAVE_A || tile == WAVE_B)
                && (wavePoses == 0 || waveTiles[wavePoses - 1] != tile)) {
            check(wavePoses < waveTiles.length, "too many visible wave poses");
            waveTiles[wavePoses] = tile;
            waveStarts[wavePoses++] = frames;
        }
        if (wavePoses == 1 && frames - waveStarts[0] == 2)
            capture(event, "scene-wave");
        if (wavePoses < 28) return;
        int glyphs = 0, mapHash = 1;
        for (int row = 1; row < 17; row++) for (int col = 1; col < 19; col++) {
            int code = gb.getGpu().getVideoRam0().getByte(0x9800 + row * 32 + col) & 255;
            if (code >= 128 && code < 224) glyphs++;
            mapHash = mapHash * 31 + code;
        }
        // The old page remains visible until START is released. Begin the
        // second page's trace only when its patterned screen has replaced it.
        if (awaitTextPattern) {
            if (glyphs != 0) return;
            awaitTextPattern = false;
        }
        int paletteHash = 1;
        byte[] palette = gb.getGpu().captureBessBackgroundPalettes();
        for (int i = 0; i < 16; i++) paletteHash = paletteHash * 31 + palette[i];
        if (sawFadePalette && textFirstFrame < 0 && paletteHash != lastFadePaletteHash)
            fadePaletteChanges++;
        sawFadePalette = true;
        lastFadePaletteHash = paletteHash;
        if (fadePaletteChanges == 4 && textFirstFrame < 0)
            capture(event, pageCapturePrefix + "-fade");

        if (glyphs == 0) return;
        if (textFirstFrame < 0) textFirstFrame = frames;
        if (sawTextMap && mapHash != lastTextHash) {
            textMapChanges++;
            textLastChangeFrame = frames;
        }
        sawTextMap = true;
        lastTextHash = mapHash;
        if (textMapChanges == 20) capture(event, pageCapturePrefix + "-reveal");

        int[] pixels = event.pixels();
        int borderHash = 1;
        for (int row = 0; row < 144; row++) for (int col = 0; col < 160; col++)
            if (row < 8 || row >= 136 || col < 8 || col >= 152)
                borderHash = borderHash * 31 + pixels[row * 160 + col];
        if (sawBorder && borderHash != lastBorderHash) textBorderChanges++;
        sawBorder = true;
        lastBorderHash = borderHash;
    }

    private void capture(Display.GbcFrameReadyEvent event, String name) {
        if (captures == null || !captured.add(name)) return;
        int[] pixels = new int[160 * 144];
        event.toRgb(pixels, false);
        BufferedImage image = new BufferedImage(160, 144, BufferedImage.TYPE_INT_RGB);
        image.setRGB(0, 0, 160, 144, pixels, 0, 160);
        try {
            ImageIO.write(image, "png", captures.resolve(name + ".png").toFile());
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
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
    private int word(int address) { return b(address) | b(address + 1) << 8; }
    private long dword(int address) {
        return (long)b(address) | (long)b(address + 1) << 8
                | (long)b(address + 2) << 16 | (long)b(address + 3) << 24;
    }
    private void put(int address, int value) { gb.getAddressSpace().setByte(address, value & 255); }
    private void putWord(int address, int value) { put(address, value); put(address + 1, value >> 8); }
    private int cell(int x, int y) { return board + (x * HEIGHT + y) * CELL_BYTES; }
    private int tile(int x, int y) {
        return gb.getGpu().getVideoRam0().getByte(0x9c00 + y * 32 + x) & 255;
    }
    private boolean at(int address) {
        return gb.getCpu().getState() == Cpu.State.OPCODE
                && gb.getCpu().getRegisters().getPC() == (address & 65535)
                && (address < 0x4000 || b(currentBank) == address >> 16);
    }
    private void until(BooleanSupplier condition, String description) {
        long deadline = ticks + 120_000_000;
        while (!condition.getAsBoolean()) {
            check(ticks < deadline, "timeout waiting for " + description
                    + " at PC=" + Integer.toHexString(gb.getCpu().getRegisters().getPC()));
            gb.tick();
            ticks++;
        }
    }
    private void next(int address) {
        until(() -> !at(address), "leave function entry");
        until(() -> at(address), "function " + Integer.toHexString(address));
    }
    private void runTicks(int count) { gb.runTicks(count); ticks += count; }
    private void runFrames(int count) {
        int end = frames + count;
        until(() -> frames >= end, "display frames");
    }
    private void press(Button button) { bus.post(new ButtonPressEvent(button)); }
    private void release(Button button) { bus.post(new ButtonReleaseEvent(button)); }
    private void tap(Button button) {
        press(button); runFrames(4); release(button); runFrames(4);
    }
    private byte[] boardSnapshot() {
        byte[] data = new byte[WIDTH * HEIGHT * CELL_BYTES];
        for (int i = 0; i < data.length; i++) data[i] = (byte)b(board + i);
        return data;
    }
    private void textAt(int x, int y, String text) {
        for (int i = 0; i < text.length(); i++) {
            int expected = 128 + text.charAt(i) - 32;
            check(tile(x + i, y) == expected,
                    "menu/ending text " + text + " differs at " + (x + i) + "," + y);
        }
    }
    private static int endingGlyph(char c) {
        return switch (c) {
            case '~' -> 245;
            case '!' -> 247;
            case '.' -> 248;
            case ',' -> 249;
            case ':' -> 250;
            default -> 128 + c - 32;
        };
    }
    private boolean endingTextReady(int page) {
        for (int y = 0; y < 18; y++) for (int x = 0; x < 20; x++) {
            String line = ENDING_PAGES[page][y];
            int start = 1 + (18 - line.length()) / 2;
            char c = x == 0 || x == 19 || y == 0 || y == 17 ? '~'
                    : x >= start && x < start + line.length() ? line.charAt(x - start) : ' ';
            int actual = gb.getGpu().getVideoRam0().getByte(0x9800 + y * 32 + x) & 255;
            if (actual != endingGlyph(c)) return false;
        }
        return true;
    }
    private void awaitEndingPage(int page) {
        int deadline = frames + 1800;
        while (!endingTextReady(page)) {
            check(frames < deadline, "timeout waiting for complete ending page " + (page + 1));
            runFrames(1);
        }
    }
    private void traceSecondPage() {
        pageCapturePrefix = "publisher";
        textFirstFrame = -1;
        textLastChangeFrame = textMapChanges = textBorderChanges = fadePaletteChanges = 0;
        sawTextMap = sawBorder = sawFadePalette = false;
        awaitTextPattern = true;
        watchEnding = true;
    }

    private void test() {
        runTicks(5_000_000);
        press(Button.START);
        runTicks(1_200_000);
        release(Button.START);
        next(symbol("_update_game"));
        check(word(pack + 4) == 1, "fixture did not load level 1");
        int key = -1;
        for (int x = 0; x < WIDTH; x++) for (int y = 0; y < HEIGHT; y++)
            if (b(cell(x, y)) == KEY) { key = cell(x, y); break; }
        check(key >= 0, "level 1 has no key to verify board preservation");

        // A distinct score and ammo count catch an accidental new game or
        // level_init() call even when the visible playfield looks similar.
        for (int i = 0; i < 4; i++) put(score + i, 123456 >> (8 * i));
        putWord(robbo + 14, 7);
        press(Button.START);
        next(symbol("_pause_gr"));
        release(Button.START);
        runFrames(10);
        check(b(WY) == 0, "pause menu did not cover the game");
        textAt(6, 8, "RESUME");
        textAt(6, 10, "RESTART");
        textAt(6, 12, "WARP");
        textAt(6, 14, "OUTRO");
        textAt(6, 16, "QUIT");
        for (int i = 0; i < 6; i++)
            check(tile(7 + i, 5) == 139 + "123456".charAt(i) - '0',
                    "pause menu did not show the preserved score");
        check(tile(4, 8) == 230, "cursor did not start on RESUME");

        byte[] before = boardSnapshot();
        long beforeScore = dword(score);
        int beforeLevel = word(pack + 4), beforeX = word(robbo);
        int beforeY = word(robbo + 2), beforeAmmo = word(robbo + 14);
        int beforeMode = word(symbol("_game_mode"));
        byte[] beforeKey = Arrays.copyOfRange(before, key - board, key - board + CELL_BYTES);
        for (int i = 0; i < 3; i++) tap(Button.DOWN);
        check(tile(4, 14) == 230, "three DOWN presses did not select OUTRO");
        press(Button.A);
        runFrames(2); // pause_gr waits for A to be released before returning
        release(Button.A);
        next(symbol("_ending_gr_show"));
        endingStartFrame = frames;
        watchEnding = true;
        awaitEndingPage(0);
        captureNext = "text-complete";
        runFrames(2); // include the completed text in the frame-level trace
        watchEnding = false;
        check(wavePoses == 28, "expected 14 complete waves (28 visible poses), got " + wavePoses);
        check(waveStarts[0] - endingStartFrame > 220,
                "arrival and landing before the wave sequence were too short");
        for (int i = 1; i < wavePoses; i++) {
            check(waveTiles[i] != waveTiles[i - 1], "wave poses did not alternate");
            int gap = waveStarts[i] - waveStarts[i - 1];
            check(gap >= 8 && gap <= 12,
                    "wave pose " + i + " lasted " + gap + " GBC frames, expected ~8 PAL frames");
        }
        check(frames - endingStartFrame >= 800,
                "outro animation/text transition ended too early");
        check(textFirstFrame - endingStartFrame >= 875
                        && textFirstFrame - endingStartFrame <= 910,
                "scene and PAL-paced text fade ran for "
                        + (textFirstFrame - endingStartFrame) + " GBC frames");
        check(fadePaletteChanges >= 8,
                "text transition did not show a gradual palette fade");
        check(textFirstFrame > waveStarts[wavePoses - 1]
                        && textLastChangeFrame - textFirstFrame >= 90
                        && textMapChanges >= 20,
                "text did not reveal through intermediate screen states");
        check(textBorderChanges >= 8,
                "patterned border did not animate while the text appeared");
        check(b(WY) == 0 || (b(LCDC) & 0x20) == 0,
                "ending left the pause window covering its scene");
        int firstPageFrame = frames;
        System.out.printf("First outro page: scene-to-text=%d, full-text=%d, "
                        + "wave poses=%d, fade steps=%d, text states=%d over %d frames, "
                        + "border states=%d%n",
                textFirstFrame - endingStartFrame, firstPageFrame - endingStartFrame,
                wavePoses, fadePaletteChanges, textMapChanges,
                textLastChangeFrame - textFirstFrame, textBorderChanges);
        traceSecondPage();
        press(Button.START);
        runFrames(120);
        check(endingTextReady(0), "held START advanced before release or skipped the publisher page");
        check((b(LCDC) & 0x20) == 0, "first page START triggered the final wipe");
        release(Button.START);
        int secondPageStart = frames;
        awaitEndingPage(1);
        captureNext = "publisher-complete";
        runFrames(2);
        watchEnding = false;
        check(textFirstFrame - secondPageStart >= 50,
                "publisher page skipped its PAL-paced fade");
        check(fadePaletteChanges >= 7,
                "publisher page did not show a gradual palette fade");
        check(textLastChangeFrame - textFirstFrame >= 90 && textMapChanges >= 20,
                "publisher page did not reveal through intermediate screen states");
        check(textBorderChanges >= 8, "publisher page border did not animate");
        check((b(LCDC) & 0x20) == 0, "publisher page started the final wipe without a second START");
        System.out.printf("Publisher page: full-text=%d frames, fade steps=%d, "
                        + "text states=%d over %d frames, border states=%d%n",
                frames - secondPageStart, fadePaletteChanges, textMapChanges,
                textLastChangeFrame - textFirstFrame, textBorderChanges);
        int exitStartFrame = frames;
        watchWipe = true;
        press(Button.START);
        runFrames(2); // ending_gr_show also waits for the confirming key to go up
        release(Button.START);
        next(symbol("_render_gr_camera_resume"));
        watchWipe = false;
        check(sawWipeWindow && wipeMovement >= 20
                        && frames - exitStartFrame >= 130,
                "closing raster wipe did not sweep the screen before returning");
        check(word(pack + 4) == beforeLevel, "outro preview changed the selected level");
        check(dword(score) == beforeScore, "outro preview changed the score");
        check(word(robbo) == beforeX && word(robbo + 2) == beforeY,
                "outro preview moved Robbo");
        check(word(robbo + 14) == beforeAmmo, "outro preview changed ammo");
        check(word(symbol("_game_mode")) == beforeMode,
                "outro preview entered the canonical end/game mode");
        check(Arrays.equals(boardSnapshot(), before), "outro preview changed the live board");
        check(Arrays.equals(Arrays.copyOfRange(boardSnapshot(), key - board,
                key - board + CELL_BYTES), beforeKey), "outro preview changed a key cell");
        runFrames(12);
        check(b(WY) == 128 && (b(LCDC) & 0x20) != 0,
                "game HUD was not restored after the outro");
        int previewCue = b(NR42) >> 4;
        check(previewCue > 0, "closing sound cue stopped after gameplay resumed"
                + " (NR42=" + Integer.toHexString(b(NR42)) + ")");
        System.out.printf("Outro exit: wipe steps=%d over %d frames, return cue=%d%n",
                wipeMovement, frames - exitStartFrame, previewCue);
        captureNext = "game-return";
        runFrames(2);
        press(Button.START);
        next(symbol("_pause_gr"));
        release(Button.START);
        runFrames(10);
        textAt(6, 14, "OUTRO");
        check(b(WY) == 0, "game cannot be paused again after the outro");
        for (int i = 0; i < 4; i++) tap(Button.DOWN);
        check(tile(4, 16) == 230, "four DOWN presses did not select QUIT");
        press(Button.A);
        runFrames(2);
        release(Button.A);
        next(symbol("_title_gr_show"));
        System.out.println("PASS pause-menu OUTRO preview, both text pages, held START, state preservation, second pause and QUIT");
    }

    private void testCanonicalEnding() {
        runTicks(5_000_000);
        press(Button.START);
        runTicks(1_200_000);
        release(Button.START);
        next(symbol("_update_game"));
        // The capsule sets END_SCREEN. Enter that production branch directly;
        // this check concerns its distinct return route, not puzzle completion.
        putWord(symbol("_game_mode"), 2);
        next(symbol("_ending_gr_show"));
        awaitEndingPage(0);
        tap(Button.START);
        awaitEndingPage(1);
        press(Button.START);
        runFrames(2);
        release(Button.START);
        next(symbol("_title_gr_show"));
        check(word(symbol("_game_mode")) == 1,
                "canonical ending did not reset game mode for the title");
        int titleEntryFrame = frames;
        until(() -> (b(LCDC) & 0x80) != 0, "canonical title display enabled");
        runFrames(8);
        int titleCue = b(NR42) >> 4;
        check(titleCue > 0,
                "closing cue stopped on the canonical title screen"
                        + " (NR42=" + Integer.toHexString(b(NR42))
                        + ", title load=" + (frames - titleEntryFrame - 8) + " frames)");
        System.out.println("PASS canonical END_SCREEN reaches title with closing cue audible (volume "
                + titleCue + " at entry)");
    }

    @Override public void close() { try { gb.close(); } finally { bus.close(); } }
    public static void main(String[] args) throws Exception {
        if (args.length < 2 || args.length > 3)
            throw new IllegalArgumentException("Usage: OutroTest ROM.gbc ROM.noi [CAPTURE_DIR]");
        Path captures = args.length == 3 ? Path.of(args[2]) : null;
        try (OutroTest test = new OutroTest(Path.of(args[0]), Path.of(args[1]), captures)) {
            test.test();
        }
        try (OutroTest test = new OutroTest(Path.of(args[0]), Path.of(args[1]), null)) {
            test.testCanonicalEnding();
        }
    }
}
