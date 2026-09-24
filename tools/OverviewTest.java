/* Coffee GB regression for the live, held-B overview.
 *
 * java --class-path "$CAMERA_TEST_CP" tools/OverviewTest.java ROM.gbc ROM.noi \
 *     [OUTPUT_DIRECTORY]
 * Use sidecars (including glue.sym beside ROM.noi) from the same ROM build.
 * Captures are native 160x144
 * frames without CGB LCD colour correction. The final-room birds retain their
 * real movement; RAM setup puts Robbo at the bottom with one shot and removes
 * the lower bomb, as in the last shot of the player's puzzle.
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
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;
import java.util.function.BooleanSupplier;
import java.util.regex.Pattern;
import javax.imageio.ImageIO;

@SuppressWarnings("deprecation")
public class OverviewTest implements AutoCloseable {
    private static final int LCDC = 0xff40, SCY = 0xff42, SCX = 0xff43;
    private static final int WY = 0xff4a, WX = 0xff4b;
    private static final int EMPTY = 0, WALL = 2, BOMB = 8, BIRD = 13, LASER_D = 32;
    private static final int ROBBO_Y = 2, ALIVE = 4, AMMO = 14, MOVED = 16, SHOT = 18;
    private static final int BLOCKED = 22, CELL_BYTES = 14, WIDTH = 16, HEIGHT = 31;
    private final EventBus bus = new EventBusImpl();
    private final Gameboy gb;
    private final Map<String, Integer> symbols = new HashMap<>();
    private final int board, robbo, mode, currentBank, update, overviewActive, overviewTop;
    private final int overviewUpdate, cameraUpdate, clockVblank, clockPhase, clockDue;
    private final Path output;
    private final int[] pixels = new int[160 * 144];
    private long ticks;
    private int frames;
    private boolean observeTiming, observeOverview, inOverview, inClock;
    private long overviewStart, maximumFullTicks, maximumOtherTicks;
    private int overviewStartFrame, beforeTop, beforeRequested, fullCalls, maximumFullFrameSpan;
    private int overviewReturnAddress, overviewReturnStack;
    private int scheduledTicks, missedTicks;
    private boolean checkWalkingMap, inCamera;
    private int checkedWalkingFrames;

    private OverviewTest(Path rom, Path noi, Path output) throws Exception {
        this.output = output;
        Files.createDirectories(output);
        var pattern = Pattern.compile("^DEF\\s+(\\S+)\\s+0x([0-9a-fA-F]+)$");
        for (String line : Files.readAllLines(noi)) {
            var match = pattern.matcher(line);
            if (match.matches())
                symbols.put(match.group(1), Integer.parseInt(match.group(2), 16));
        }
        board = symbol("_board");
        robbo = symbol("_robbo");
        mode = symbol("_game_mode");
        currentBank = symbol("__current_bank");
        update = symbol("_update_game");
        overviewActive = symbol("_gr_overview_active");
        overviewTop = symbol("_gr_overview_top");
        overviewUpdate = symbol("_overview_gr_update");
        cameraUpdate = symbol("_render_gr_camera");
        Map<String, Integer> glue = new HashMap<>();
        pattern = Pattern.compile("^\\s*\\d+\\s+(\\S+)\\s+([0-9a-fA-F]{8})\\s+[A-Z]+\\s*$");
        for (String line : Files.readAllLines(noi.resolveSibling("glue.sym"))) {
            var match = pattern.matcher(line);
            if (match.matches()) glue.put(match.group(1), Integer.parseInt(match.group(2), 16));
        }
        clockVblank = symbol("_main") - glue.get("_main") + glue.get("_game_clock_vblank");
        gb = new Gameboy.GameboyConfiguration(rom.toFile())
                .setBootstrapMode(Gameboy.BootstrapMode.SKIP)
                .setGameboyType(GameboyType.CGB).setSupportBatterySave(false).build();
        gb.init(bus, SerialEndpoint.NULL_ENDPOINT, null);
        // This static timer has no exported WRAM symbol. Validate SDCC's first
        // LD HL,game_tick_phase and use matching local offsets for the due flag.
        check(b(clockVblank) == 0x21, "cannot locate game clock storage: expected LD HL,nn");
        clockPhase = word(clockVblank + 1);
        clockDue = clockPhase + glue.get("_game_tick_due") - glue.get("_game_tick_phase");
        bus.register((Display.GbcFrameReadyEvent event) -> {
            frames++;
            event.toRgb(pixels, false);
        }, Display.GbcFrameReadyEvent.class);
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
    private void put(int address, int value) { gb.getAddressSpace().setByte(address, value & 255); }
    private void putWord(int address, int value) { put(address, value); put(address + 1, value >> 8); }
    private int cell(int x, int y) { return board + (x * HEIGHT + y) * CELL_BYTES; }
    private boolean at(int address) {
        return gb.getCpu().getState() == Cpu.State.OPCODE
                && gb.getCpu().getRegisters().getPC() == (address & 65535)
                && (address < 0x4000 || b(currentBank) == address >> 16);
    }
    private void until(BooleanSupplier condition, String description) {
        long limit = ticks + 40_000_000;
        while (!condition.getAsBoolean()) {
            check(ticks < limit, "timeout: " + description);
            if (observeTiming) observeClock();
            if (observeOverview) observeOverviewCall();
            if (checkWalkingMap) observeWalkingMap();
            gb.tick();
            ticks++;
        }
    }
    private void next(int address) {
        until(() -> !at(address), "leave function");
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
        press(button); runFrames(5); release(button); runFrames(5);
    }
    private void position(int x, int y) { putWord(robbo, x); putWord(robbo + ROBBO_Y, y); }
    private void clearCell(int x, int y) {
        int address = cell(x, y);
        if ((b(address + 13) & 4) != 0) {
            int row = symbol("_gr_row_active") + y;
            put(row, b(row) - 1);
        }
        for (int i = 0; i < CELL_BYTES; i++) put(address + i, 0);
        put(address + 11, b(symbol("_cycle_count")));
        put(address + 12, 2); // empty cells remain blowable
        put(address + 13, 2); // redraw
    }
    private void dirtyBoard() {
        for (int x = 0; x < WIDTH; x++) for (int y = 0; y < HEIGHT; y++)
            put(cell(x, y) + 13, b(cell(x, y) + 13) | 2);
    }
    private void capture(String name) throws Exception {
        BufferedImage image = new BufferedImage(160, 144, BufferedImage.TYPE_INT_RGB);
        image.setRGB(0, 0, 160, 144, pixels, 0, 160);
        ImageIO.write(image, "png", output.resolve(name + ".png").toFile());
    }
    private int tile(int address) { return gb.getGpu().getVideoRam0().getByte(address) & 255; }
    private int attr(int address) { return gb.getGpu().getVideoRam1().getByte(address) & 255; }
    private int overviewCell(int x, int y) {
        int row = y - b(overviewTop);
        check(row >= 0 && row < 16, "board row " + y + " outside overview");
        return 0x9c00 + (row + 2) * 32 + x + 2;
    }
    private void checkOverviewCell(int x, int y, int glyph, int palette) {
        int address = overviewCell(x, y);
        check(tile(address) == glyph, String.format(
                "overview (%d,%d) has tile %02x, expected %02x", x, y, tile(address), glyph));
        check(attr(address) == (8 | palette),
                "overview cell uses the wrong palette or tile-data bank at " + x + "," + y);
    }
    private void checkOverview(int top) {
        check(b(overviewActive) != 0 && (b(LCDC) & 8) != 0,
                "held B did not select the overview background map");
        check(b(SCX) == 0 && b(SCY) == 16 && b(overviewTop) == top,
                "overview origin/scroll does not match the clamped board position");
        checkHud();
        // Verify the complete left and right edges, not just an isolated tile.
        // These cells are walls in both the top and bottom 16 rows of level 56.
        for (int y = top; y < top + 16; y++) {
            for (int x : new int[] {0, 15}) {
                if (b(cell(x, y)) == WALL) {
                    int state = b(cell(x, y) + 2);
                    checkOverviewCell(x, y, 0, state == 3 ? 2 : state == 9 ? 0 : 1);
                }
            }
        }
        checkOverviewCell(word(robbo), word(robbo + ROBBO_Y), 0x5e, 0);
    }
    private void checkNormal() {
        check(b(overviewActive) == 0 && (b(LCDC) & 8) == 0,
                "release did not restore the normal background map");
        checkHud();
    }
    private void checkHud() {
        check((b(LCDC) & 0x60) == 0x60 && b(WY) == 128 && b(WX) == 7,
                "HUD window was displaced or hidden");
        for (int y = 0; y < 2; y++) for (int x = 0; x < 20; x++)
            check(attr(0x9c00 + y * 32 + x) == 6,
                    "overview corrupted HUD attributes at " + x + "," + y);
        int ammo = word(robbo + AMMO);
        check(tile(0x9c00 + 12) == 139 + ammo / 10
                        && tile(0x9c00 + 13) == 139 + ammo % 10,
                "HUD ammo is stale or overwritten");
        check(tile(0x9c00 + 17) == 144 && tile(0x9c00 + 18) == 145,
                "HUD level 56 digits were overwritten");
    }
    private int birds() {
        int positions = 0;
        for (int x = 0; x < WIDTH; x++) if (b(cell(x, 21)) == BIRD) positions |= 1 << x;
        return positions;
    }
    private void checkBirds() {
        int positions = birds();
        check(Integer.bitCount(positions) == 2, "last-room corridor no longer has two live birds");
        for (int x = 1; x < 15; x++) {
            int type = b(cell(x, 21));
            if (type == BIRD) checkOverviewCell(x, 21, 0x12, 0);
            else if (type == EMPTY) checkOverviewCell(x, 21, 0x40, 0);
        }
    }
    private byte[] staticGraphics() {
        byte[] result = new byte[(4 + 96) * 16];
        int offset = 0;
        for (int t : new int[] {0, 1, 32, 33})
            for (int i = 0; i < 16; i++) result[offset++] = (byte)tile(0x8000 + t * 16 + i);
        for (int t = 128; t < 224; t++)
            for (int i = 0; i < 16; i++) result[offset++] = (byte)tile(0x8000 + t * 16 + i);
        return result;
    }

    private void bootFinalRoom() {
        runTicks(5_000_000);
        press(Button.START);
        runTicks(1_200_000);
        release(Button.START);
        next(symbol("_level_init"));
        putWord(symbol("_level_packs") + 4, 56);
        next(update);
        check(b(cell(10, 22)) == BOMB && b(cell(10, 26)) == BOMB && birds() != 0,
                "final-room puzzle or build symbols changed");
        position(10, 29);
        putWord(robbo + ALIVE, 1);
        putWord(robbo + AMMO, 1);
        putWord(robbo + MOVED, 0);
        putWord(robbo + SHOT, 0);
        putWord(robbo + BLOCKED, 0);
        clearCell(10, 26);
        dirtyBoard();
        runFrames(160);
        next(update); // stop after a completed render, before the next actor update
    }

    private void observeOverviewCall() {
        int address = overviewUpdate;
        if (!inOverview && at(address)) {
            inOverview = true;
            overviewStart = ticks;
            overviewStartFrame = frames;
            beforeTop = b(overviewTop);
            beforeRequested = b(symbol("_gr_overview_requested"));
            int sp = gb.getCpu().getRegisters().getSP();
            overviewReturnAddress = word(sp);
            overviewReturnStack = (sp + 2) & 65535;
        } else if (inOverview && gb.getCpu().getState() == Cpu.State.OPCODE
                && gb.getCpu().getRegisters().getPC() == overviewReturnAddress
                && gb.getCpu().getRegisters().getSP() == overviewReturnStack) {
            // Match the saved return address and stack depth, including when
            // the graphics and overview modules share the same ROM bank.
            long duration = ticks - overviewStart;
            if (beforeTop != b(overviewTop)
                    || beforeRequested == 0 && b(symbol("_gr_overview_requested")) != 0) {
                fullCalls++;
                maximumFullTicks = Math.max(maximumFullTicks, duration);
                maximumFullFrameSpan = Math.max(maximumFullFrameSpan, frames - overviewStartFrame);
            } else maximumOtherTicks = Math.max(maximumOtherTicks, duration);
            inOverview = false;
        }
    }

    private void observeClock() {
        boolean entered = at(clockVblank);
        if (entered && !inClock && word(clockPhase) + 5488 >= 23009) {
            scheduledTicks++;
            if (b(clockDue) != 0) missedTicks++;
        }
        inClock = entered;
    }

    private void observeWalkingMap() {
        boolean entered = at(cameraUpdate);
        if (entered && !inCamera && b(overviewActive) != 0) {
            int top = Math.max(0, Math.min(15, word(robbo + ROBBO_Y) - 8));
            checkOverview(top);
            for (int y = top; y < top + 16; y++) {
                if (y == 0 || y == HEIGHT - 1) continue;
                checkOverviewCell(3, y, word(robbo + ROBBO_Y) == y ? 0x5e : 0x40, 0);
                checkOverviewCell(8, y, (y & 1) == 0 ? 0 : 0x40, (y & 1) == 0 ? 1 : 0);
            }
            checkedWalkingFrames++;
        }
        inCamera = entered;
    }

    private void test() throws Exception {
        bootFinalRoom();
        checkNormal();
        byte[] normalGraphics = staticGraphics();
        capture("level-56-normal");

        press(Button.B);
        for (int i = 0; i < 4; i++) next(update);
        checkOverview(15);
        checkBirds();
        checkOverviewCell(10, 22, 0x58, 0);
        int oldBirds = birds();
        boolean birdsMoved = false;
        for (int i = 0; i < 8; i++) {
            next(update);
            checkBirds();
            birdsMoved |= birds() != oldBirds;
        }
        check(birdsMoved, "overview paused the birds or displayed a stale board");
        check(word(robbo) == 10 && word(robbo + ROBBO_Y) == 29 && word(robbo + AMMO) == 1,
                "B alone moved Robbo or consumed his last shot");
        capture("level-56-overview");
        System.out.println("PASS live overview: both birds, upper bomb and Robbo visible together");

        press(Button.A);
        press(Button.UP);
        for (int i = 0; i < 5 && word(robbo + AMMO) != 0; i++) next(update);
        check(word(robbo + AMMO) == 0 && word(robbo + SHOT) > 0,
                "B+A+UP did not fire the last bullet");
        check(word(robbo) == 10 && word(robbo + ROBBO_Y) == 29,
                "shooting in overview moved Robbo");
        boolean projectile = false;
        for (int y = 23; y < 29; y++) projectile |= b(cell(10, y)) == LASER_D;
        check(projectile, "ammo changed without creating a real upward projectile");
        release(Button.A);
        release(Button.UP);
        for (int i = 0; i < 25 && b(cell(10, 22)) == BOMB; i++) next(update);
        check(b(cell(10, 22)) != BOMB, "overview shot failed to reach the upper bomb");
        runFrames(4);
        next(update);
        checkOverview(15);
        capture("level-56-shot");
        release(Button.B);
        runFrames(20);
        next(update);
        checkNormal();
        check(word(robbo + AMMO) == 0, "leaving overview reset ammo");
        check(Arrays.equals(normalGraphics, staticGraphics()),
                "overview damaged the normal playfield or HUD tile graphics");
        capture("level-56-return");
        System.out.println("PASS B+A+UP fires a real shot and detonates the upper bomb; release restores normal view");

        // Camera limits are independent of hazards. Freeze the engine while
        // moving its target directly, keeping real input, camera and rendering.
        putWord(mode, 0);
        runFrames(20);
        position(3, 1); // empty floor; (1,1) contains a screw in level 56
        press(Button.B);
        runFrames(30);
        checkOverview(0);
        capture("level-56-upper-overview");
        position(10, 29);
        runFrames(30);
        checkOverview(15);
        release(Button.B);
        runFrames(180);
        checkNormal();
        check(b(SCX) == 88 && b(SCY) == (368 & 255),
                "normal camera did not return to its bottom target");
        System.out.println("PASS overview clamps at both level edges and normal camera recovers");

        press(Button.B);
        runFrames(20);
        press(Button.START);
        runFrames(10);
        release(Button.B);
        release(Button.START);
        runFrames(20);
        check(b(WY) == 0 && b(overviewActive) == 0 && (b(LCDC) & 8) == 0,
                "pause menu did not disable the overview");
        capture("paused-from-overview");
        tap(Button.START); // Resume.
        runFrames(20);
        checkNormal();
        press(Button.B);
        runFrames(30);
        checkOverview(15); // Menu has overwritten the shared map: it must be rebuilt.
        check(Arrays.equals(normalGraphics, staticGraphics()), "menu/overview transition damaged font data");
        capture("overview-after-pause");

        press(Button.START);
        runFrames(10);
        release(Button.START);
        release(Button.B);
        runFrames(20);
        tap(Button.DOWN); // Restart, one below Resume.
        tap(Button.A);
        runFrames(40);
        checkNormal();
        check(word(robbo) == 2 && word(robbo + ROBBO_Y) == 26,
                "restart did not restore the authentic level 56 starting position");
        check(b(symbol("_gr_overview_requested")) == 0,
                "restart retained a stale overview request");
        check(b(cell(10, 22)) == BOMB && b(cell(10, 26)) == BOMB,
                "restart failed to restore the room");
        System.out.println("PASS pause/resume redraws shared map and restart clears overview state");
        System.out.println("Overview functional checks passed; screenshots: " + output.toAbsolutePath());
    }

    private record Cadence(int frames, int maximumGap, int scheduled, int missed,
                           int[] birds, int[] playerRows) { }
    private Cadence measureCadence(boolean overview, boolean walking) {
        bootFinalRoom();
        if (walking) {
            // A safe empty corridor isolates rendering while actual held-DOWN
            // input moves the target through many changes of overview top row.
            for (int x = 1; x < WIDTH - 1; x++) for (int y = 1; y < HEIGHT - 1; y++)
                clearCell(x, y);
            // Alternating static markers expose a wrongly shifted/duplicated
            // row even when most of the safe corridor consists of empty floor.
            for (int y = 2; y < HEIGHT - 1; y += 2) put(cell(8, y), WALL);
            position(3, 7);
            putWord(robbo + MOVED, 0);
            dirtyBoard();
        }
        if (overview) press(Button.B);
        for (int i = 0; i < 8; i++) next(update); // initial tile upload is outside the measurement
        if (walking) press(Button.DOWN);
        int count = walking ? 80 : 120;
        int[] birdTrace = new int[count], playerTrace = new int[count];
        int first = frames, previous = frames, maximumGap = 0;
        observeTiming = true;
        observeOverview = overview;
        checkWalkingMap = overview && walking;
        for (int i = 0; i < count; i++) {
            if (walking && i == count / 2) {
                release(Button.DOWN);
                press(Button.UP);
            }
            next(update);
            maximumGap = Math.max(maximumGap, frames - previous);
            previous = frames;
            birdTrace[i] = birds();
            playerTrace[i] = word(robbo + ROBBO_Y);
        }
        if (walking) {
            release(Button.UP);
            check(playerTrace[count / 2 - 1] > 20 && playerTrace[count - 1] < 10,
                    "cadence fixture did not walk down and back across overview rows");
            if (overview) check(checkedWalkingFrames > 80, "walking map checks were not exercised");
        }
        checkWalkingMap = false;
        observeOverview = false;
        observeTiming = false;
        if (overview) System.out.printf(
                "Overview uploads: full=%d maxFull=%d ticks (%d frame events), maxOther=%d ticks%n",
                fullCalls, maximumFullTicks, maximumFullFrameSpan, maximumOtherTicks);
        return new Cadence(frames - first, maximumGap, scheduledTicks, missedTicks, birdTrace, playerTrace);
    }

    private static void compareCadence(Path rom, Path noi, Path output, boolean walking) throws Exception {
        Cadence normal, overview;
        try (OverviewTest test = new OverviewTest(rom, noi, output)) {
            normal = test.measureCadence(false, walking);
        }
        try (OverviewTest test = new OverviewTest(rom, noi, output)) {
            overview = test.measureCadence(true, walking);
        }
        String scenario = walking ? "walking across overview rows" : "level 56 birds";
        check(Arrays.equals(normal.birds, overview.birds)
                        && Arrays.equals(normal.playerRows, overview.playerRows),
                scenario + ": overview changed gameplay at equal engine updates");
        System.out.printf("Cadence %-30s normal=%d frames (max gap %d, dropped %d/%d), "
                        + "overview=%d frames (max gap %d, dropped %d/%d)%n",
                scenario, normal.frames, normal.maximumGap, normal.missed, normal.scheduled,
                overview.frames, overview.maximumGap, overview.missed, overview.scheduled);
        // Timing at function entry can vary with render latency. Check actual
        // coalesced clock flags instead of imposing a frame-alignment tolerance.
        check(overview.missed <= normal.missed,
                scenario + ": overview rendering dropped additional game-clock ticks");
        System.out.println("PASS " + scenario + ": same actor trace and sustained gameplay cadence");
    }

    @Override public void close() { try { gb.close(); } finally { bus.close(); } }
    public static void main(String[] args) throws Exception {
        if (args.length < 2 || args.length > 3)
            throw new IllegalArgumentException("Usage: OverviewTest ROM.gbc ROM.noi [OUTPUT_DIRECTORY]");
        Path output = args.length == 3 ? Path.of(args[2]) : Path.of("build/overview-captures");
        Path rom = Path.of(args[0]), noi = Path.of(args[1]);
        try (OverviewTest test = new OverviewTest(rom, noi, output)) {
            test.test();
        }
        compareCadence(rom, noi, output, true);
        compareCadence(rom, noi, output, false);
        System.out.println("Overview regression passed.");
    }
}
