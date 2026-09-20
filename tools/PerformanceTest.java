/* Deterministic emulated-time performance/board regression (Coffee GB core).
 *
 * java --class-path "$PERFORMANCE_TEST_CP" tools/PerformanceTest.java ROM.gbc
 * Optional: ROM.noi level[,level...] cycles output-directory
 * Defaults: matching ROM.noi, 1,4,11,16,27,35,45,50,51,54, 128 cycles.
 *
 * A fresh emulator boots each level through the real loader/main loop. Only
 * level_selected is changed, immediately before its first level_init call.
 * Measurements span complete game updates and use 4,194,304 emulated master
 * ticks/second, not host speed. Phase times include callees and interrupts.
 * Snapshots omit presentation/cache fields (redraw, inlist, processed), and
 * can be compared across builds with diff -rq output-before output-after.
 * -Dperformance.assertRowCounts=true checks the exact active-row invariant.
 * -Dperformance.initialCycle=257 seeds the cycle and processed bytes at load,
 * for comparing the same workload before/after the 8-bit stamp wraps.
 * -Dperformance.initialFrame=65520 seeds sys_time before loading the level,
 * for checking scheduler behavior when the 16-bit VBlank counter wraps.
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
    private static final double MASTER_HZ = 4_194_304.0;
    private final EventBus bus = new EventBusImpl();
    private final Gameboy gb;
    private final Map<String, Integer> symbols;
    private final int board, robbo, currentBank;
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
        for (String name : List.of("update_game", "show_game_area", "render_gr_camera",
                "render_gr_anim", "render_gr_robbo")) phases.add(new Phase(name));
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
        double render = phases.get(1).elapsed / MASTER_HZ;
        System.out.printf(Locale.ROOT,
                "L%-2d cycles=%d frames=%d seconds=%.6f cycles/s=%.3f active=%d rows=%d logic=%.1f%% render=%.1f%% snapshot=%s%n",
                level, completed, frames - initialFrames, seconds, completed / seconds,
                active, rows, logic / seconds * 100, render / seconds * 100,
                HexFormat.of().formatHex(digest.digest()).substring(0, 16));
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
            Files.write(output.resolve(String.format(Locale.ROOT, "level-%02d.bin", level)), joined);
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 1 || args.length > 5)
            throw new IllegalArgumentException("Usage: PerformanceTest ROM.gbc [ROM.noi] [levels] [cycles] [snapshot-directory]");
        Path rom = Path.of(args[0]).toAbsolutePath();
        Path noi = args.length > 1 ? Path.of(args[1])
                : rom.resolveSibling(rom.getFileName().toString().replaceFirst("\\.[^.]+$", ".noi"));
        String levels = args.length > 2 ? args[2] : "1,4,11,16,27,35,45,50,51,54";
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
