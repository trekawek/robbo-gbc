/* Deterministic emulated-time performance/board regression (Coffee GB core).
 *
 * java --class-path "$PERFORMANCE_TEST_CP" tools/PerformanceTest.java ROM.gbc
 * Optional: ROM.noi level[,level...] cycles output-directory
 * Defaults: matching ROM.noi, 1,4,11,16,18,27,35,45,50,51,54, 128 cycles.
 *
 * A fresh emulator boots each level through the real loader/main loop. By
 * default only level_selected is changed, before its first level_init call.
 * Measurements span complete game updates and use 4,194,304 emulated master
 * ticks/second, not host speed. Phase times include callees and interrupts.
 * Snapshots omit presentation/cache fields (redraw, inlist, processed), and
 * can be compared across builds with diff -rq output-before output-after.
 * -Dperformance.assertRowCounts=true checks the exact active-row invariant.
 * -Dperformance.initialCycle=257 seeds the cycle and processed bytes at load,
 * for comparing the same workload before/after the 8-bit stamp wraps.
 * -Dperformance.initialFrame=65520 seeds sys_time before loading the level,
 * for checking scheduler behavior when the 16-bit VBlank counter wraps.
 * -Dperformance.laserScenario=beams|disconnected|edges|crossings replaces the
 * loaded room with bounded beam fixtures. Compare 32 ticks across ROMs with
 * assertRowCounts=true and separate snapshot directories.
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
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HexFormat;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.regex.Pattern;

@SuppressWarnings("deprecation") // Keep compatibility with older Coffee GB cores.
public class PerformanceTest {
    private static final int WIDTH = 16, HEIGHT = 31, CELL_BYTES = 14;
    private static final int EMPTY = 0, WALL = 2, LASER_L = 30, LASER_D = 32, GUN = 50;
    private static final int[] DX = {1, 0, -1, 0}, DY = {0, 1, 0, -1};
    private static final double MASTER_HZ = 4_194_304.0;
    private final EventBus bus = new EventBusImpl();
    private final Gameboy gb;
    private final Map<String, Integer> symbols;
    private final int board, robbo, currentBank;
    private final String laserScenario = System.getProperty("performance.laserScenario");
    private int frames;
    private long ticks;

    private PerformanceTest(Path rom, Path noi) throws Exception {
        symbols = new HashMap<>();
        var pattern = Pattern.compile("^DEF\\s+(\\S+)\\s+0x([0-9a-fA-F]+)$");
        for (String line : Files.readAllLines(noi)) {
            var match = pattern.matcher(line);
            if (match.matches()) symbols.put(match.group(1),
                    Integer.parseInt(match.group(2), 16));
        }
        board = symbol("_board");
        robbo = symbol("_robbo");
        currentBank = symbol("__current_bank");
        gb = new Gameboy.GameboyConfiguration(rom.toFile())
                .setBootstrapMode(Gameboy.BootstrapMode.SKIP)
                .setGameboyType(GameboyType.CGB).setSupportBatterySave(false).build();
        gb.init(bus, SerialEndpoint.NULL_ENDPOINT, null);
        bus.register((Display.GbcFrameReadyEvent event) -> frames++,
                Display.GbcFrameReadyEvent.class);
    }

    private int symbol(String name) {
        Integer address = symbols.get(name);
        if (address == null) throw new IllegalArgumentException("Missing symbol " + name);
        return address;
    }
    private int b(int address) { return gb.getAddressSpace().getByte(address) & 255; }
    private int w(int address) { return b(address) | b(address + 1) << 8; }
    private void putWord(int address, int value) {
        gb.getAddressSpace().setByte(address, value & 255);
        gb.getAddressSpace().setByte(address + 1, value >> 8 & 255);
    }
    private boolean at(int address) {
        return gb.getCpu().getState() == Cpu.State.OPCODE
                && gb.getCpu().getRegisters().getPC() == (address & 65535)
                && (address < 0x4000 || b(currentBank) == address >> 16);
    }
    private void tick() { gb.tick(); ticks++; }
    private void reach(String function) {
        int address = symbol(function);
        long deadline = ticks + 15_000_000;
        do {
            if (ticks >= deadline) throw new AssertionError("Never reached " + function
                    + " pc=" + Integer.toHexString(gb.getCpu().getRegisters().getPC())
                    + " state=" + gb.getCpu().getState()
                    + " cycle=" + w(symbol("_cycle_count")) + " frames=" + frames);
            tick();
        } while (!at(address));
    }
    private void boot(int level) {
        gb.runTicks(5_000_000);
        ticks += 5_000_000;
        bus.post(new ButtonPressEvent(Button.START));
        gb.runTicks(1_200_000);
        ticks += 1_200_000;
        bus.post(new ButtonReleaseEvent(Button.START));
        reach("_level_init");
        // level_packs[0].level_selected is the third 16-bit field (game.h).
        putWord(symbol("_level_packs") + 4, level);
        if (System.getProperty("performance.initialFrame") != null)
            putWord(symbol("_sys_time"), Integer.getInteger("performance.initialFrame"));
        reach("_update_game");
        if (w(symbol("_level")) != WIDTH || w(symbol("_level") + 2) != HEIGHT)
            throw new AssertionError("Unexpected board layout or mismatched ROM symbols");
        if (System.getProperty("performance.initialCycle") != null) {
            int cycle = Integer.getInteger("performance.initialCycle");
            putWord(symbol("_cycle_count"), cycle);
            for (int cell = 0; cell < WIDTH * HEIGHT; cell++)
                gb.getAddressSpace().setByte(board + cell * CELL_BYTES + 11, cycle & 255);
        }
        if (laserScenario != null) seedLasers();
    }

    // Fixtures use the board.h layout, just like snapshot(). Guns stay on
    // cooldown for the 32-tick comparison so beam interactions are isolated.
    private int cellAddress(int x, int y) { return board + (x * HEIGHT + y) * CELL_BYTES; }
    private void putByte(int address, int value) {
        gb.getAddressSpace().setByte(address, value & 255);
    }
    private void laserCell(int x, int y, int type, int direction, boolean returning) {
        int address = cellAddress(x, y);
        boolean active = type == GUN || type == LASER_L || type == LASER_D;
        for (int field = 0; field < CELL_BYTES; field++) putByte(address + field, 0);
        putByte(address, type);
        putByte(address + 2, type == GUN ? direction : active ? 2 : 0);
        putByte(address + 3, direction);
        putByte(address + 7, active ? 1 : 0); // solid laser
        putByte(address + 9, type == GUN ? 255 : 0);
        putByte(address + 11, w(symbol("_cycle_count")) - 1);
        putByte(address + 12, (type == WALL ? 0 : 2) | (returning ? 128 : 0));
        putByte(address + 13, 2 | (active ? 4 : 0)); // redraw, inlist
    }
    private void beam(int x, int y, int direction, int length, boolean returning) {
        laserCell(x, y, GUN, direction, false);
        for (int i = 1; i <= length; i++)
            laserCell(x + DX[direction] * i, y + DY[direction] * i,
                    (direction & 1) == 0 ? LASER_L : LASER_D,
                    direction, returning && i == length);
    }
    private void seedLasers() {
        for (int x = 0; x < WIDTH; x++)
            for (int y = 0; y < HEIGHT; y++) laserCell(x, y, WALL, 0, false);
        // Robbo has a separate, walled-off pocket below the test fixtures.
        for (int field = 0; field < 28; field++) putByte(robbo + field, 0);
        putWord(robbo, 1);
        putWord(robbo + 2, 29);
        putWord(robbo + 4, 1);
        putWord(robbo + 8, 2);
        putWord(robbo + 10, 1);
        laserCell(1, 29, EMPTY, 0, false);
        switch (laserScenario) {
            case "beams" -> {
                // Growing beams and returning tips in every direction.
                int[][] starts = {{1, 2, 0}, {2, 8, 1}, {14, 5, 2}, {5, 15, 3}};
                for (int[] start : starts) {
                    int x = start[0], y = start[1], dir = start[2];
                    beam(x, y, dir, 3, false);
                    for (int i = 4; i <= 5; i++)
                        laserCell(x + DX[dir] * i, y + DY[dir] * i, EMPTY, 0, false);
                }
                beam(1, 18, 0, 4, true);
                beam(8, 23, 1, 4, true);
                beam(14, 20, 2, 4, true);
                beam(12, 27, 3, 4, true);
            }
            case "disconnected" -> {
                beam(1, 2, 0, 6, false);
                laserCell(1, 2, EMPTY, 0, false); // missing source
                beam(3, 8, 1, 7, false);
                laserCell(3, 11, EMPTY, 0, false); // gap in the middle
                beam(14, 5, 2, 5, false);
                beam(9, 23, 3, 7, false);
                putByte(cellAddress(14, 5) + 12, 2 | 8); // gun destroyed this tick
                putByte(cellAddress(9, 23) + 12, 2 | 8);
            }
            case "edges" -> {
                beam(11, 2, 0, 4, false);
                beam(3, 26, 1, 4, false);
                beam(4, 5, 2, 4, false);
                beam(6, 4, 3, 4, false);
                // Orphaned beams whose backwards origin search reaches an edge.
                int[][] starts = {{0, 10, 0}, {9, 0, 1}, {15, 13, 2}, {12, 30, 3}};
                for (int[] start : starts) {
                    int x = start[0], y = start[1], dir = start[2];
                    beam(x, y, dir, 4, false);
                    laserCell(x, y, (dir & 1) == 0 ? LASER_L : LASER_D, dir, false);
                }
            }
            case "crossings" -> {
                beam(1, 2, 0, 6, false); // opposing tips touch
                beam(14, 2, 2, 6, false);
                beam(1, 6, 0, 5, false);
                laserCell(4, 6, LASER_D, 0, false); // mixed type, same direction
                beam(1, 12, 0, 6, false);
                beam(5, 9, 1, 6, false); // perpendicular crossing at (5,12)
                beam(1, 18, 0, 7, false);
                laserCell(4, 18, LASER_L, 2, false); // same type, opposite direction
            }
            default -> throw new IllegalArgumentException("Unknown laser scenario: " + laserScenario);
        }
        for (int y = 0; y < HEIGHT; y++) {
            int count = 0;
            for (int x = 0; x < WIDTH; x++)
                if ((b(cellAddress(x, y) + 13) & 4) != 0) count++;
            putByte(symbol("_gr_row_active") + y, count);
        }
    }

    private byte[] snapshot() {
        if (Boolean.getBoolean("performance.assertRowCounts")) {
            for (int y = 0; y < HEIGHT; y++) {
                int count = 0;
                for (int x = 0; x < WIDTH; x++)
                    if ((b(board + (x * HEIGHT + y) * CELL_BYTES + 13) & 4) != 0) count++;
                int actual = b(symbol("_gr_row_active") + y);
                if (count != actual) throw new AssertionError("Active row " + y
                        + " has " + count + " cells but cached count " + actual
                        + " at cycle " + w(symbol("_cycle_count")));
            }
        }
        // 11 byte scalar fields + behavior bitfield A + shooting flag per cell.
        byte[] result = new byte[WIDTH * HEIGHT * 13 + 28 + 4];
        int p = 0;
        for (int cell = 0; cell < WIDTH * HEIGHT; cell++) {
            int address = board + cell * CELL_BYTES;
            for (int field = 0; field <= 10; field++) result[p++] = (byte) b(address + field);
            result[p++] = (byte) b(address + 12);
            result[p++] = (byte) (b(address + 13) & 1);
        }
        for (int field = 0; field < 28; field++) result[p++] = (byte) b(robbo + field);
        for (String field : List.of("_game_mode", "_restart_timeout")) {
            int address = symbol(field);
            result[p++] = (byte) b(address);
            result[p++] = (byte) b(address + 1);
        }
        return result;
    }

    private final class Phase {
        final String name;
        final int address;
        long elapsed, started;
        int returnPc, returnSp, calls;
        boolean active;
        Phase(String name) { this.name = name; address = symbol("_" + name); }
        void sample() {
            var registers = gb.getCpu().getRegisters();
            if (active) {
                if (registers.getPC() == returnPc && registers.getSP() == returnSp
                        && gb.getCpu().getState() == Cpu.State.OPCODE) {
                    elapsed += ticks - started;
                    calls++;
                    active = false;
                }
            } else if (at(address)) {
                started = ticks;
                int sp = registers.getSP();
                returnPc = w(sp);
                returnSp = sp + 2 & 65535;
                active = true;
            }
        }
    }

    private void measure(int level, int cycles, Path output) throws Exception {
        boot(level);
        var phases = new ArrayList<Phase>();
        for (String name : List.of("update_game", "upd_g4", "upd_g5", "show_game_area",
                "render_gr_camera", "render_gr_anim", "render_gr_robbo"))
            phases.add(new Phase(name));
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        List<byte[]> snapshots = new ArrayList<>();
        long started = ticks, deadline = ticks + (long) cycles * 4_194_304;
        int initialFrames = frames, completed = 0, active = 0, rows = 0;
        int initialClock = w(symbol("_sys_time")), previousFrame = frames;
        int minimumFrameGap = Integer.MAX_VALUE, maximumFrameGap = 0;
        for (int y = 0; y < HEIGHT; y++) {
            boolean row = false;
            for (int x = 0; x < WIDTH; x++) {
                if ((b(board + (x * HEIGHT + y) * CELL_BYTES + 13) & 4) != 0) {
                    active++;
                    row = true;
                }
            }
            if (row) rows++;
        }
        byte[] first = snapshot();
        digest.update(first);
        snapshots.add(first);
        boolean wasUpdate = true;
        while (completed < cycles) {
            for (Phase phase : phases) phase.sample();
            tick();
            boolean nowUpdate = at(symbol("_update_game"));
            if (nowUpdate && !wasUpdate) {
                completed++;
                int gap = frames - previousFrame;
                minimumFrameGap = Math.min(minimumFrameGap, gap);
                maximumFrameGap = Math.max(maximumFrameGap, gap);
                previousFrame = frames;
                byte[] next = snapshot();
                digest.update(next);
                snapshots.add(next);
            }
            wasUpdate = nowUpdate;
            if (ticks >= deadline) throw new AssertionError("Game failed to advance at L" + level);
        }
        double seconds = (ticks - started) / MASTER_HZ;
        double logic = phases.get(0).elapsed / MASTER_HZ;
        double render = phases.get(3).elapsed / MASTER_HZ;
        System.out.printf(Locale.ROOT,
                "L%-2d cycles=%d frames=%d seconds=%.6f cycles/s=%.3f active=%d rows=%d logic=%.1f%% render=%.1f%% snapshot=%s%n",
                level, completed, frames - initialFrames, seconds, completed / seconds,
                active, rows, logic / seconds * 100, render / seconds * 100,
                HexFormat.of().formatHex(digest.digest()).substring(0, 16));
        if (laserScenario != null) System.out.println("  laser scenario=" + laserScenario);
        if (System.getProperty("performance.initialFrame") != null)
            System.out.printf("  frame clock=%d -> %d update intervals=%d..%d frames%n",
                    initialClock, w(symbol("_sys_time")), minimumFrameGap, maximumFrameGap);
        for (Phase phase : phases) System.out.printf(Locale.ROOT,
                "  %-18s calls=%d mean=%.3fms total=%.3fms%n", phase.name,
                phase.calls, phase.calls == 0 ? 0 : phase.elapsed * 1000 / MASTER_HZ / phase.calls,
                phase.elapsed * 1000 / MASTER_HZ);
        if (output != null) {
            Files.createDirectories(output);
            byte[] joined = new byte[first.length * snapshots.size()];
            for (int i = 0; i < snapshots.size(); i++)
                System.arraycopy(snapshots.get(i), 0, joined, i * first.length, first.length);
            String suffix = laserScenario == null ? "" : "-" + laserScenario;
            Files.write(output.resolve(String.format(Locale.ROOT, "level-%02d%s.bin", level, suffix)), joined);
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 1 || args.length > 5)
            throw new IllegalArgumentException("Usage: PerformanceTest ROM.gbc [ROM.noi] [levels] [cycles] [snapshot-directory]");
        Path rom = Path.of(args[0]).toAbsolutePath();
        Path noi = args.length > 1 ? Path.of(args[1])
                : rom.resolveSibling(rom.getFileName().toString().replaceFirst("\\.[^.]+$", ".noi"));
        String levels = args.length > 2 ? args[2] : "1,4,11,16,18,27,35,45,50,51,54";
        int cycles = args.length > 3 ? Integer.parseInt(args[3]) : 128;
        if (cycles <= 0) throw new IllegalArgumentException("cycles must be positive");
        Path output = args.length > 4 ? Path.of(args[4]) : null;
        for (String level : levels.split(",")) {
            PerformanceTest test = new PerformanceTest(rom, noi);
            try { test.measure(Integer.parseInt(level), cycles, output); }
            finally { test.gb.close(); }
        }
    }
}
