/* Emulator regression for the camera. Requires an already built Coffee GB core
 * and its dependencies; CAMERA_TEST_CP is that complete Java classpath.
 *
 * java --class-path "$CAMERA_TEST_CP" tools/CameraTest.java build/robbo.gbc
 * Optional arguments: ROM.noi render.sym (default: beside the ROM).
 * The sidecars must come from the same build as the ROM.
 */
import eu.rekawek.coffeegb.core.Gameboy;
import eu.rekawek.coffeegb.core.GameboyType;
import eu.rekawek.coffeegb.core.events.EventBus;
import eu.rekawek.coffeegb.core.events.EventBusImpl;
import eu.rekawek.coffeegb.core.gpu.Display;
import eu.rekawek.coffeegb.core.joypad.Button;
import eu.rekawek.coffeegb.core.joypad.ButtonPressEvent;
import eu.rekawek.coffeegb.core.joypad.ButtonReleaseEvent;
import eu.rekawek.coffeegb.core.serial.SerialEndpoint;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;
import java.util.regex.Pattern;

public class CameraTest {
    private static final int SCY = 0xff42, SCX = 0xff43, LY = 0xff44, LCDC = 0xff40;
    // Field offsets in board.h's robbo and object structs, not WRAM addresses.
    private static final int ROBBO_Y = 2, ALIVE = 4, MOVED = 16, BLOCKED = 22;
    private static final int CELL_BYTES = 14, WIDTH = 16, HEIGHT = 31;
    private static final int MAX_X = WIDTH * 16 - 160, MAX_Y = HEIGHT * 16 - 128;
    private final EventBus bus = new EventBusImpl();
    private final Gameboy gb;
    private final int robbo, board, mode, restart, slotOwner, cameraX, cameraY;
    private boolean watching;
    private int frames, scrollX, scrollY, absoluteY, previousX, previousY;
    private int changes, maximumStep, phaseStart;
    private String phase = "boot";

    private CameraTest(Path rom, Path noi, Path sym) throws Exception {
        Map<String, Integer> globals = symbols(noi,
                "^DEF\\s+(\\S+)\\s+0x([0-9a-fA-F]+)$");
        Map<String, Integer> locals = symbols(sym,
                "^\\s*\\d+\\s+(\\S+)\\s+([0-9a-fA-F]{8})\\s+[A-Z]+\\s*$");
        // Use a global in render.c's _DATA area to relocate its static symbols.
        int renderData = symbol(globals, "_gr_logo_rainbow")
                - symbol(locals, "_gr_logo_rainbow");
        slotOwner = renderData + symbol(locals, "_slot_owner");
        cameraX = renderData + symbol(locals, "_camera_x");
        cameraY = renderData + symbol(locals, "_camera_y");
        robbo = symbol(globals, "_robbo");
        board = symbol(globals, "_board");
        mode = symbol(globals, "_game_mode");
        restart = symbol(globals, "_restart_timeout");
        gb = new Gameboy.GameboyConfiguration(rom.toFile())
                .setBootstrapMode(Gameboy.BootstrapMode.SKIP)
                .setGameboyType(GameboyType.CGB).setSupportBatterySave(false).build();
        gb.init(bus, SerialEndpoint.NULL_ENDPOINT, null);
        bus.register((Display.GbcFrameReadyEvent event) -> frame(),
                Display.GbcFrameReadyEvent.class);
        gb.runTicks(5_000_000);
        bus.post(new ButtonPressEvent(Button.START));
        gb.runTicks(1_200_000);
        bus.post(new ButtonReleaseEvent(Button.START));
        gb.runTicks(12_000_000);
        int level = symbol(globals, "_level");
        check(word(level) == WIDTH && word(level + 2) == HEIGHT,
                "unexpected level dimensions or mismatched symbols");
        check(word(robbo) == 2 && word(robbo + ROBBO_Y) == 2,
                "game did not reach the first level");
        // Isolate camera targets first, retaining the real renderer and main loop.
        putWord(mode, 0);
        scrollX = previousX = byteAt(SCX);
        scrollY = byteAt(SCY);
        absoluteY = previousY = word(cameraY) >> 4;
        watching = true;
    }

    private static Map<String, Integer> symbols(Path path, String regex) throws Exception {
        var result = new HashMap<String, Integer>();
        var pattern = Pattern.compile(regex);
        for (String line : Files.readAllLines(path)) {
            var match = pattern.matcher(line);
            if (match.matches()) result.put(match.group(1),
                    Integer.parseInt(match.group(2), 16));
        }
        return result;
    }

    private static int symbol(Map<String, Integer> symbols, String name) {
        Integer value = symbols.get(name);
        if (value == null) throw new IllegalArgumentException("Missing build symbol: " + name);
        return value;
    }

    private int byteAt(int address) { return gb.getAddressSpace().getByte(address) & 255; }
    private int word(int address) { return byteAt(address) | byteAt(address + 1) << 8; }
    private void putByte(int address, int value) { gb.getAddressSpace().setByte(address, value & 255); }
    private void putWord(int address, int value) {
        putByte(address, value);
        putByte(address + 1, value >> 8);
    }

    private void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(phase + " at frame " + frames + ": " + message);
    }

    private void frame() {
        frames++;
        if (!watching) return;
        int step = Math.max(Math.abs(scrollX - previousX), Math.abs(absoluteY - previousY));
        maximumStep = Math.max(maximumStep, step);
        check(step <= 3, "camera moved " + step + " pixels in one frame");
        check(scrollX >= 0 && scrollX <= MAX_X && absoluteY >= 0 && absoluteY <= MAX_Y,
                "camera escaped the level bounds");
        for (int row = absoluteY / 16; row <= (absoluteY + 127) / 16; row++) {
            check(byteAt(slotOwner + (row & 15)) == row, "unloaded visible row " + row);
        }
        previousX = scrollX;
        previousY = absoluteY;
    }

    private void runFrames(int count) {
        int end = frames + count;
        long limit = (long) count * 300_000;
        while (frames < end && limit-- > 0) {
            gb.tick();
            int x = byteAt(SCX), y = byteAt(SCY);
            if (x != scrollX || y != scrollY) {
                check((byteAt(LCDC) & 128) == 0 || byteAt(LY) >= 144,
                        "scroll register changed during active display at LY=" + byteAt(LY));
                // Unwrap the hardware's 8-bit SCY into the full 496-pixel board.
                absoluteY += ((y - scrollY + 128) & 255) - 128;
                scrollX = x;
                scrollY = y;
                changes++;
            }
        }
        check(frames == end, "display stopped producing frames");
    }

    private void begin(String name) {
        phase = name;
        phaseStart = frames;
        changes = maximumStep = 0;
    }

    private void position(int x, int y) {
        putWord(robbo, x);
        putWord(robbo + ROBBO_Y, y);
    }

    private void settled() {
        int x = Math.max(0, Math.min(MAX_X, word(robbo) * 16 - 72));
        int y = Math.max(0, Math.min(MAX_Y, word(robbo + ROBBO_Y) * 16 - 56));
        check(word(cameraX) == x * 16 && word(cameraY) == y * 16,
                "camera did not converge to " + x + "," + y);
        check(scrollX == x && absoluteY == y, "scroll registers disagree with settled position");
        check(changes > 0, "phase did not exercise scrolling");
        System.out.printf("PASS %-24s frames=%d changes=%d maxStep=%d end=%d,%d%n",
                phase, frames - phaseStart, changes, maximumStep, scrollX, absoluteY);
    }

    private void target(String name, int x, int y, int duration) {
        begin(name);
        position(x, y);
        runFrames(duration);
        settled();
    }

    private void held(String name, Button button, int x, int y, int duration) {
        begin(name);
        bus.post(new ButtonPressEvent(button));
        runFrames(duration);
        bus.post(new ButtonReleaseEvent(button));
        runFrames(120);
        check(word(robbo) == x && word(robbo + ROBBO_Y) == y,
                "held input did not reach expected corridor endpoint");
        settled();
    }

    private void test() {
        target("horizontal forward", 12, 2, 180);
        target("horizontal reverse", 1, 2, 180);
        target("vertical forward wrap", 1, 28, 400);
        target("vertical reverse wrap", 1, 1, 400);
        begin("reverse while moving");
        position(12, 28);
        runFrames(20);
        check(scrollX > 0 && absoluteY > 0 && absoluteY < MAX_Y,
                "camera was not moving before reversal");
        position(1, 1);
        runFrames(180);
        settled();
        target("diagonal", 6, 8, 180);

        // Restore real input/game pacing on a safe, empty interior. The boundary
        // walls remain intact; structure offsets match board.h.
        for (int x = 1; x < WIDTH - 1; x++) {
            for (int y = 1; y < HEIGHT - 1; y++) {
                int cell = board + (x * HEIGHT + y) * CELL_BYTES;
                for (int offset = 0; offset < CELL_BYTES; offset++) putByte(cell + offset, 0);
            }
        }
        putWord(robbo + ALIVE, 1);
        putWord(robbo + BLOCKED, 0);
        putWord(robbo + MOVED, 0);
        putWord(restart, 0);
        putWord(mode, 1);
        held("continuous down", Button.DOWN, 6, 29, 600);
        held("continuous right", Button.RIGHT, 14, 29, 300);
        held("continuous up", Button.UP, 14, 1, 750);
        held("continuous left", Button.LEFT, 1, 1, 450);
        System.out.println("Camera regression passed: VBlank scrolling, <=3 px/frame, loaded rows and convergence.");
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 1 || args.length > 3) {
            throw new IllegalArgumentException("Usage: CameraTest ROM.gbc [ROM.noi] [render.sym]");
        }
        Path rom = Path.of(args[0]).toAbsolutePath();
        String stem = rom.getFileName().toString().replaceFirst("\\.[^.]+$", "");
        Path noi = args.length > 1 ? Path.of(args[1]) : rom.resolveSibling(stem + ".noi");
        Path sym = args.length > 2 ? Path.of(args[2]) : rom.resolveSibling("render.sym");
        CameraTest test = new CameraTest(rom, noi, sym);
        try { test.test(); } finally { test.gb.close(); }
    }
}
