/* Level 52 Atari barricade hit and direction regression (Coffee GB core).
 *
 * java --class-path "$COLOR_TEST_CP" tools/BarrierTest.java ROM.gbc ROM.noi
 * The ROM and linker symbols must come from the same build.
 *
 * Check that the first shot into a solid run explodes, then remove one
 * segment and check that the resulting gap moves left as on Atari.
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
import java.util.HashMap;
import java.util.Map;
import java.util.function.BooleanSupplier;
import java.util.regex.Pattern;

@SuppressWarnings("deprecation")
public class BarrierTest implements AutoCloseable {
    private static final int HEIGHT = 31, CELL_BYTES = 14;
    private static final int EMPTY = 0, WALL = 2, BIG_BOOM = 42, BARRIER = 61, WEST = 2;
    private final EventBus bus = new EventBusImpl();
    private final Gameboy gb;
    private final Map<String, Integer> symbols = new HashMap<>();
    private final int board, robbo, currentBank, update;
    private long ticks;

    private BarrierTest(Path rom, Path noi) throws Exception {
        var pattern = Pattern.compile("^DEF\\s+(\\S+)\\s+0x([0-9a-fA-F]+)$");
        for (String line : Files.readAllLines(noi)) {
            var match = pattern.matcher(line);
            if (match.matches())
                symbols.put(match.group(1), Integer.parseInt(match.group(2), 16));
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
        Integer value = symbols.get(name);
        if (value == null) throw new IllegalArgumentException("Missing symbol " + name);
        return value;
    }
    private int b(int address) { return gb.getAddressSpace().getByte(address) & 255; }
    private void put(int address, int value) { gb.getAddressSpace().setByte(address, value); }
    private void putWord(int address, int value) {
        put(address, value & 255);
        put(address + 1, value >> 8);
    }
    private int cell(int x, int y) { return board + (x * HEIGHT + y) * CELL_BYTES; }
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    private boolean at(int address) {
        return gb.getCpu().getState() == Cpu.State.OPCODE
                && gb.getCpu().getRegisters().getPC() == (address & 65535)
                && (address < 0x4000 || b(currentBank) == address >> 16);
    }
    private void until(BooleanSupplier condition) {
        long limit = ticks + 30_000_000;
        while (!condition.getAsBoolean()) {
            check(ticks < limit, "timeout waiting for engine update");
            gb.tick();
            ticks++;
        }
    }
    private void next(int address) {
        until(() -> !at(address));
        until(() -> at(address));
    }

    private void loadLevel52() {
        gb.runTicks(5_000_000);
        ticks += 5_000_000;
        bus.post(new ButtonPressEvent(Button.START));
        gb.runTicks(1_200_000);
        ticks += 1_200_000;
        bus.post(new ButtonReleaseEvent(Button.START));
        next(symbol("_level_init"));
        putWord(symbol("_level_packs") + 4, 52);
        next(update);
    }

    private void firstShot() {
        loadLevel52();
        check(b(cell(7, 14)) == EMPTY && b(cell(7, 13)) == BARRIER,
                "first-shot fixture is blocked");
        putWord(robbo, 7);
        putWord(robbo + 2, 14);
        putWord(robbo + 14, 1); // one bullet, aimed up at the solid barricade
        putWord(robbo + 18, 0);
        bus.post(new ButtonPressEvent(Button.A));
        bus.post(new ButtonPressEvent(Button.UP));
        int explosionStates = 0;
        for (int tick = 0; tick < 8; tick++) {
            next(update);
            for (int x = 3; x <= 12; x++)
                if (b(cell(x, 13)) == BIG_BOOM)
                    explosionStates |= 1 << b(cell(x, 13) + 2);
        }
        bus.post(new ButtonReleaseEvent(Button.UP));
        bus.post(new ButtonReleaseEvent(Button.A));
        check(b(robbo + 14) == 0, "Robbo did not fire into the barricade");
        check(Integer.bitCount(explosionStates) >= 2,
                "first barricade hit never animated its explosion");
        boolean openedGap = false;
        for (int tick = 0; tick < 12; tick++) {
            next(update);
            for (int x = 3; x <= 12; x++)
                openedGap |= b(cell(x, 13)) == EMPTY;
        }
        check(openedGap, "barricade did not resume after the explosion");
        System.out.println("PASS level 52 first barricade hit animates its explosion");
    }

    private void run() {
        loadLevel52();

        check(b(cell(2, 13)) == WALL && b(cell(13, 13)) == WALL,
                "level 52 barricade is not bounded by walls");
        for (int x = 3; x <= 12; x++)
            check(b(cell(x, 13)) == BARRIER && b(cell(x, 13) + 3) == WEST,
                    "barricade segment at x=" + x + " is not westbound");

        put(cell(7, 13), EMPTY); // a shot removes one segment
        int previous = 7, shifts = 0;
        boolean wrapped = false;
        for (int i = 0; i < 30; i++) {
            next(update);
            check(b(cell(2, 13)) == WALL && b(cell(13, 13)) == WALL,
                    "barricade escaped its endpoint walls");
            int gap = -1;
            for (int x = 3; x <= 12; x++) {
                int type = b(cell(x, 13));
                check(type == EMPTY || type == BARRIER, "unexpected barricade cell");
                if (type == EMPTY) {
                    check(gap == -1, "barricade created a second gap");
                    gap = x;
                }
            }
            check(gap != -1, "barricade gap disappeared");
            if (gap == previous) continue;
            check(gap == (previous == 3 ? 12 : previous - 1),
                    "barricade gap moved right or skipped a cell: " + previous + " to " + gap);
            if (previous == 3) wrapped = true;
            previous = gap;
            shifts++;
        }
        check(shifts >= 5 && wrapped, "barricade gap did not shift left and wrap");
        System.out.println("PASS level 52 barricade gap shifts left and wraps between its walls");
    }

    @Override public void close() { try { gb.close(); } finally { bus.close(); } }
    public static void main(String[] args) throws Exception {
        if (args.length != 2)
            throw new IllegalArgumentException("Usage: BarrierTest ROM.gbc ROM.noi");
        try (BarrierTest test = new BarrierTest(Path.of(args[0]), Path.of(args[1]))) {
            test.firstShot();
        }
        try (BarrierTest test = new BarrierTest(Path.of(args[0]), Path.of(args[1]))) {
            test.run();
        }
    }
}
