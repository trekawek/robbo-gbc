/* Emulated-time scheduler and held-movement regression using Coffee GB.
 *
 * java --class-path "$TIMING_TEST_CP" tools/TimingTest.java ROM.gbc [ROM.noi] [updates]
 * Sidecar symbols must belong to the ROM. Defaults to 64 update intervals.
 * -Dtiming.minimumFrames=N asserts the shortest allowed update interval.
 * -Dtiming.assertPal=true checks Atari PAL cadence (7 frames per cell step).
 *
 * Timing uses emulated master ticks, never host wall time. Real production
 * input, scheduler, rendering and camera code remain active. The quiet-room
 * cases replace the board in RAM with an empty bordered room; busy-room cases
 * retain the authentic loaded board. Function addresses come from the linker.
 */
import eu.rekawek.coffeegb.core.Gameboy;
import eu.rekawek.coffeegb.core.GameboyType;
import eu.rekawek.coffeegb.core.cpu.Cpu;
import eu.rekawek.coffeegb.core.events.EventBus;
import eu.rekawek.coffeegb.core.events.EventBusImpl;
import eu.rekawek.coffeegb.core.joypad.Button;
import eu.rekawek.coffeegb.core.joypad.ButtonPressEvent;
import eu.rekawek.coffeegb.core.joypad.ButtonReleaseEvent;
import eu.rekawek.coffeegb.core.serial.SerialEndpoint;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.function.BooleanSupplier;
import java.util.regex.Pattern;

@SuppressWarnings("deprecation")
public class TimingTest implements AutoCloseable {
    private static final int WIDTH = 16, HEIGHT = 31, CELL_BYTES = 14;
    private static final double MASTER_HZ = 4_194_304.0;
    private static final double FRAME_MS = 70224 * 1000.0 / MASTER_HZ;
    private static final double PAL_HALF_MS = 7 * 312 * 114 * 1000.0 / (2 * 1_773_447);
    private static final boolean ASSERT_PAL = Boolean.getBoolean("timing.assertPal");
    private static final int MINIMUM_FRAMES = Integer.getInteger("timing.minimumFrames", 1);
    private final EventBus bus = new EventBusImpl();
    private final Gameboy gb;
    private final Map<String, Integer> symbols = new HashMap<>();
    private final int board, robbo, currentBank, update;
    private long ticks;
    private String phase = "boot";
    private final List<Stamp> animation = new ArrayList<>();
    private int animationAddress, animationValue;
    private boolean watchAnimation;

    private TimingTest(Path rom, Path noi) throws Exception {
        var pattern = Pattern.compile("^DEF\\s+(\\S+)\\s+0x([0-9a-fA-F]+)$");
        for (String line : Files.readAllLines(noi)) {
            var match = pattern.matcher(line);
            if (match.matches()) symbols.put(match.group(1),
                    Integer.parseInt(match.group(2), 16));
        }
        board = symbol("_board");
        robbo = symbol("_robbo");
        currentBank = symbol("__current_bank");
        update = symbol("_update_game");
        gb = new Gameboy.GameboyConfiguration(rom.toFile())
                .setBootstrapMode(Gameboy.BootstrapMode.SKIP)
                .setGameboyType(GameboyType.CGB).setSupportBatterySave(false).build();
        gb.init(bus, SerialEndpoint.NULL_ENDPOINT, null);
    }

    private int symbol(String name) {
        Integer result = symbols.get(name);
        if (result == null) throw new IllegalArgumentException("Missing symbol " + name);
        return result;
    }
    private int b(int address) { return gb.getAddressSpace().getByte(address) & 255; }
    private int w(int address) { return b(address) | b(address + 1) << 8; }
    private void put(int address, int value) { gb.getAddressSpace().setByte(address, value & 255); }
    private void putWord(int address, int value) { put(address, value); put(address + 1, value >> 8); }
    private int cell(int x, int y) { return board + (x * HEIGHT + y) * CELL_BYTES; }
    private void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(phase + ": " + message);
    }
    private boolean at(int address) {
        return gb.getCpu().getState() == Cpu.State.OPCODE
                && gb.getCpu().getRegisters().getPC() == (address & 65535)
                && (address < 0x4000 || b(currentBank) == address >> 16);
    }
    private void tick() {
        gb.tick(); ticks++;
        if (watchAnimation && b(animationAddress) != animationValue) {
            animationValue = b(animationAddress);
            animation.add(stamp());
        }
    }
    private void runTicks(int count) { gb.runTicks(count); ticks += count; }
    private void until(BooleanSupplier condition, String description) {
        long deadline = ticks + 30_000_000;
        while (!condition.getAsBoolean()) {
            check(ticks < deadline, "timeout waiting for " + description
                    + " pc=" + Integer.toHexString(gb.getCpu().getRegisters().getPC()));
            tick();
        }
    }
    private void next(int address) {
        // A function-entry opcode may remain visible for multiple master ticks.
        until(() -> !at(address), "leaving function entry");
        until(() -> at(address), "function " + Integer.toHexString(address));
    }
    private void next(String name) { next(symbol(name)); }
    private void press(Button key) { bus.post(new ButtonPressEvent(key)); }
    private void release(Button key) { bus.post(new ButtonReleaseEvent(key)); }
    private void boot(int level, boolean wrap) {
        runTicks(5_000_000);
        press(Button.START);
        runTicks(1_200_000);
        release(Button.START);
        next("_level_init");
        putWord(symbol("_level_packs") + 4, level);
        if (wrap) putWord(symbol("_sys_time"), 65500);
        next(update);
        check(w(symbol("_level")) == WIDTH && w(symbol("_level") + 2) == HEIGHT,
                "unexpected board dimensions or mismatched symbols");
    }

    private void quietRoom() {
        // Preserve the real packed-object layout and active-row invariant.
        for (int x = 0; x < WIDTH; x++) for (int y = 0; y < HEIGHT; y++) {
            int address = cell(x, y);
            for (int field = 0; field < CELL_BYTES; field++) put(address + field, 0);
            put(address, x == 0 || x == WIDTH - 1 || y == 0 || y == HEIGHT - 1 ? 2 : 0);
            put(address + 12, 2); // blowable
            put(address + 13, 2); // redraw
        }
        for (int y = 0; y < HEIGHT; y++) put(symbol("_gr_row_active") + y, 0);
        putWord(robbo, 2);
        putWord(robbo + 2, 2);
        putWord(robbo + 4, 1);
        putWord(robbo + 16, 0);
        putWord(robbo + 22, 0);
        putWord(robbo + 26, 0);
        putWord(symbol("_restart_timeout"), 0);
        // Let the one-time full redraw finish before measuring steady timing.
        for (int i = 0; i < 6; i++) next(update);
    }

    private record Stamp(long ticks, int frame) { }
    private Stamp stamp() { return new Stamp(ticks, w(symbol("_sys_time"))); }
    private List<Stamp> updates(int count) {
        List<Stamp> samples = new ArrayList<>();
        if (!at(update)) next(update);
        samples.add(stamp());
        for (int i = 0; i < count; i++) {
            next(update);
            samples.add(stamp());
        }
        return samples;
    }
    private void report(String label, List<Stamp> samples, boolean updates) {
        long sum = 0, minimum = Long.MAX_VALUE, maximum = 0;
        int minFrames = Integer.MAX_VALUE, maxFrames = 0;
        for (int i = 1; i < samples.size(); i++) {
            Stamp a = samples.get(i - 1), b = samples.get(i);
            long interval = b.ticks - a.ticks;
            int frames = b.frame - a.frame & 65535;
            sum += interval;
            minimum = Math.min(minimum, interval);
            maximum = Math.max(maximum, interval);
            minFrames = Math.min(minFrames, frames);
            maxFrames = Math.max(maxFrames, frames);
            if (updates) check(frames >= MINIMUM_FRAMES,
                    "update burst: " + frames + " VBlanks (minimum " + MINIMUM_FRAMES + ")");
        }
        int intervals = samples.size() - 1;
        double mean = sum * 1000.0 / MASTER_HZ / intervals;
        System.out.printf(Locale.ROOT,
                "%-23s n=%-3d mean=%8.3fms min=%8.3fms max=%8.3fms rate=%6.3f/s frames=%d..%d%n",
                label, intervals, mean, minimum * 1000.0 / MASTER_HZ,
                maximum * 1000.0 / MASTER_HZ, 1000 / mean, minFrames, maxFrames);
    }
    private void assertPal(List<Stamp> samples, double period, boolean busy) {
        if (!ASSERT_PAL) return;
        double elapsed = (samples.get(samples.size() - 1).ticks - samples.get(0).ticks)
                * 1000.0 / MASTER_HZ;
        double expected = period * (samples.size() - 1);
        // Busy function-entry times include variable work before the call;
        // allow one half-step of boundary latency, but no sustained overspeed.
        double tolerance = busy ? PAL_HALF_MS + FRAME_MS : FRAME_MS;
        check(elapsed >= expected - tolerance && (busy || elapsed <= expected + tolerance),
                String.format(Locale.ROOT, "PAL cadence mismatch: %.3fms elapsed, %.3fms expected%s",
                        elapsed, expected, busy ? " minimum" : ""));
    }

    private void quiet(int count, boolean wrap) {
        phase = wrap ? "quiet frame wrap" : "quiet updates";
        boot(1, wrap);
        int initialClock = w(symbol("_sys_time"));
        quietRoom();
        if (!wrap && symbols.containsKey("_gr_anim_frame")) {
            animationAddress = symbol("_gr_anim_frame");
            animationValue = b(animationAddress);
            watchAnimation = true;
        }
        List<Stamp> samples = updates(count);
        watchAnimation = false;
        report(phase, samples, true);
        assertPal(samples, PAL_HALF_MS, false);
        if (animation.size() > 1) {
            report("ambient animation", animation, false);
            assertPal(animation, PAL_HALF_MS * 4, false);
        }
        if (wrap) {
            int endClock = w(symbol("_sys_time"));
            check(endClock < initialClock, "test did not cross the 16-bit VBlank wrap");
            System.out.printf("  frame clock wrapped %d -> %d%n", initialClock, endClock);
        }
    }
    private void movement() {
        phase = "held down movement";
        boot(1, false);
        quietRoom();
        List<Stamp> samples = new ArrayList<>();
        int previousY = w(robbo + 2), iterations = 0;
        press(Button.DOWN);
        while (samples.size() < 21 && iterations++ < 150) {
            next(update);
            int y = w(robbo + 2);
            if (y != previousY) {
                check(y == previousY + 1 && w(robbo) == 2, "unexpected movement in empty corridor");
                samples.add(stamp());
                previousY = y;
            }
        }
        release(Button.DOWN);
        check(samples.size() == 21, "held input stopped moving");
        report(phase, samples, false);
        assertPal(samples, PAL_HALF_MS * 2, false);
        System.out.printf("  %d cell steps observed across %d game updates%n", samples.size(), iterations);
    }
    private void busy(int level, int count) {
        phase = "L" + level + " active objects";
        boot(level, false);
        List<Stamp> samples = updates(count);
        report(phase, samples, true);
        assertPal(samples, PAL_HALF_MS, true);
        check(w(robbo + 4) != 0 && w(symbol("_level_packs") + 4) == level,
                "busy sample changed level or killed Robbo");
        if (level == 51) {
            quietRoom();
            phase = "quiet after busy L51";
            samples = updates(16);
            report(phase, samples, true);
            assertPal(samples, PAL_HALF_MS, false);
        }
    }
    private void pause() {
        phase = "pause and resume";
        boot(1, false);
        quietRoom();
        press(Button.START);
        next("_pause_gr");
        release(Button.START);
        runTicks(1_500_000);
        int pausedCycle = w(symbol("_cycle_count"));
        runTicks(4_194_304);
        check(w(symbol("_cycle_count")) == pausedCycle, "game updates continued while paused");
        press(Button.A);
        runTicks(200_000);
        release(Button.A);
        long released = ticks;
        next(update);
        System.out.printf(Locale.ROOT, "pause resume first update %.3fms after button release%n",
                (ticks - released) * 1000.0 / MASTER_HZ);
        List<Stamp> samples = updates(16);
        report(phase, samples, true);
        assertPal(samples, PAL_HALF_MS, false);
    }

    @Override public void close() { try { gb.close(); } finally { bus.close(); } }
    public static void main(String[] args) throws Exception {
        if (args.length < 1 || args.length > 3)
            throw new IllegalArgumentException("Usage: TimingTest ROM.gbc [ROM.noi] [updates]");
        Path rom = Path.of(args[0]).toAbsolutePath();
        Path noi = args.length > 1 ? Path.of(args[1])
                : rom.resolveSibling(rom.getFileName().toString().replaceFirst("\\.[^.]+$", ".noi"));
        int count = args.length > 2 ? Integer.parseInt(args[2]) : 64;
        if (count < 16) throw new IllegalArgumentException("Use at least 16 update intervals");
        try (TimingTest test = new TimingTest(rom, noi)) { test.quiet(count, false); }
        try (TimingTest test = new TimingTest(rom, noi)) { test.movement(); }
        for (int level : new int[] {4, 51})
            try (TimingTest test = new TimingTest(rom, noi)) { test.busy(level, count); }
        try (TimingTest test = new TimingTest(rom, noi)) { test.quiet(count, true); }
        try (TimingTest test = new TimingTest(rom, noi)) { test.pause(); }
        System.out.println("PASS movement, active rooms, frame-clock wrap and pause timing");
    }
}
