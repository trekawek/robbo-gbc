/* Level 22 cannon conversion and live blaster regression (Coffee GB core).
 *
 * java --class-path "$CANNON_TEST_CP" tools/CannonTest.java ROM.gbc [ROM.noi]
 * Matching linker symbols are required; defaults to the ROM's .noi sidecar.
 *
 * Boots the real level and checks cannon configuration before changing RAM.
 * Removes only the three blocking boxes to isolate firing and debris clearing;
 * this fixture is not a solution of the level. Cannon state, random firing,
 * gameplay updates, rendering and the rest of the loaded board remain live.
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
public class CannonTest implements AutoCloseable {
    private static final int WIDTH = 16, HEIGHT = 31, CELL_BYTES = 14, UPDATES = 192;
    private static final int EMPTY = 0, SCREW = 4, BOX = 6, GROUND = 24, GUN = 50, BLASTER = 58;
    private static final int EAST = 0, BLASTER_SHOT = 2;
    private final EventBus bus = new EventBusImpl();
    private final Gameboy gb;
    private final Map<String, Integer> symbols = new HashMap<>();
    private final int board, robbo, currentBank, update;
    private long ticks;

    private CannonTest(Path rom, Path noi) throws Exception {
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
        Integer value = symbols.get(name);
        if (value == null) throw new IllegalArgumentException("Missing symbol " + name);
        return value;
    }
    private int b(int address) { return gb.getAddressSpace().getByte(address) & 255; }
    private int w(int address) { return b(address) | b(address + 1) << 8; }
    private void put(int address, int value) { gb.getAddressSpace().setByte(address, value & 255); }
    private int cell(int x, int y) { return board + (x * HEIGHT + y) * CELL_BYTES; }
    private void check(boolean condition, String message) {
        if (!condition) throw new AssertionError("Level 22: " + message);
    }
    private boolean at(int address) {
        return gb.getCpu().getState() == Cpu.State.OPCODE
                && gb.getCpu().getRegisters().getPC() == (address & 65535)
                && (address < 0x4000 || b(currentBank) == address >> 16);
    }
    private void until(BooleanSupplier condition, String description) {
        long deadline = ticks + 30_000_000;
        while (!condition.getAsBoolean()) {
            check(ticks < deadline, "timeout waiting for " + description
                    + " pc=" + Integer.toHexString(gb.getCpu().getRegisters().getPC()));
            gb.tick();
            ticks++;
        }
    }
    private void next(int address) {
        // A single opcode can remain at the entry PC for several master ticks.
        until(() -> !at(address), "leaving function entry");
        until(() -> at(address), "function " + Integer.toHexString(address));
    }
    private void runTicks(int count) { gb.runTicks(count); ticks += count; }
    private void boot() {
        runTicks(5_000_000);
        bus.post(new ButtonPressEvent(Button.START));
        runTicks(1_200_000);
        bus.post(new ButtonReleaseEvent(Button.START));
        next(symbol("_level_init"));
        // level_packs[0].level_selected is the third 16-bit field in game.h.
        put(symbol("_level_packs") + 4, 22);
        put(symbol("_level_packs") + 5, 0);
        next(update);
        check(w(symbol("_level")) == WIDTH && w(symbol("_level") + 2) == HEIGHT,
                "unexpected board dimensions or mismatched symbols");
    }

    private void assertRows() {
        for (int y = 0; y < HEIGHT; y++) {
            int expected = 0;
            for (int x = 0; x < WIDTH; x++)
                if ((b(cell(x, y) + 13) & 4) != 0) expected++;
            int actual = b(symbol("_gr_row_active") + y);
            check(actual == expected, "active row " + y + " contains " + expected
                    + " objects but cached count is " + actual
                    + " at cycle " + w(symbol("_cycle_count")));
        }
    }
    private void removeBlocker(int y) {
        int address = cell(2, y);
        check(b(address) == BOX && (b(address + 13) & 4) == 0,
                "expected an inactive blocking box at (2," + y + ")");
        // create_object(EMPTY_FIELD)'s packed layout. The inactive box and
        // empty field both contribute zero to gr_row_active, so retain it.
        for (int field = 0; field < CELL_BYTES; field++) put(address + field, 0);
        put(address, EMPTY);
        put(address + 11, w(symbol("_cycle_count"))); // processed
        put(address + 12, 2); // blowable
        put(address + 13, 2); // redraw, not inlist
    }

    private void run() {
        boot();
        assertRows();
        boolean[][] debris = new boolean[3][WIDTH];
        int[] farthestBlaster = new int[3];
        for (int y = 6; y <= 8; y++) {
            int address = cell(1, y);
            check(b(address) == GUN, "missing cannon at (1," + y + ")");
            check(b(address + 3) == EAST, "cannon (1," + y + ") faces "
                    + b(address + 3) + ", expected east (0)");
            check(b(address + 7) == BLASTER_SHOT, "cannon (1," + y
                    + ") uses shot type " + b(address + 7) + ", expected blaster (2)");
            int count = 0;
            for (int x = 0; x < WIDTH; x++) {
                debris[y - 6][x] = b(cell(x, y)) == GROUND;
                if (debris[y - 6][x]) count++;
            }
            check(count == (y == 7 ? 8 : 9), "unexpected initial debris in row " + y);
            check(b(cell(14, y)) == SCREW, "missing initial screw at (14," + y + ")");
        }
        System.out.println("Loaded Level 22: all three left cannons face east and fire blasters");
        for (int y = 6; y <= 8; y++) removeBlocker(y);
        assertRows();
        for (int step = 1; step <= UPDATES; step++) {
            next(update);
            assertRows();
            check(w(robbo + 4) != 0 && w(symbol("_restart_timeout")) == 0,
                    "Robbo died or restarted at update " + step);
            check(w(symbol("_level_packs") + 4) == 22, "level changed");
            for (int y = 6; y <= 8; y++) {
                check(b(cell(14, y)) == SCREW, "screw destroyed at (14," + y + ")");
                for (int x = 2; x < 14; x++) if (b(cell(x, y)) == BLASTER) {
                    check(b(cell(x, y) + 3) == EAST, "blaster is not travelling east");
                    farthestBlaster[y - 6] = Math.max(farthestBlaster[y - 6], x);
                }
            }
        }
        for (int y = 6; y <= 8; y++) {
            int count = 0, farthestDebris = 0;
            for (int x = 0; x < WIDTH; x++) if (debris[y - 6][x]) {
                check(b(cell(x, y)) != GROUND, "debris remains at (" + x + "," + y + ")");
                farthestDebris = x;
                count++;
            }
            check(farthestBlaster[y - 6] >= farthestDebris,
                    "no blaster observed reaching the far end of row " + y);
            System.out.printf("  Row %d: %d debris cleared; eastbound blaster reached x=%d; screw preserved%n",
                    y, count, farthestBlaster[y - 6]);
        }
        System.out.printf("PASS %d live updates; Robbo alive; exact active-row counts at every boundary%n", UPDATES);
    }

    @Override public void close() { try { gb.close(); } finally { bus.close(); } }
    public static void main(String[] args) throws Exception {
        if (args.length < 1 || args.length > 2)
            throw new IllegalArgumentException("Usage: CannonTest ROM.gbc [ROM.noi]");
        Path rom = Path.of(args[0]).toAbsolutePath();
        Path noi = args.length > 1 ? Path.of(args[1])
                : rom.resolveSibling(rom.getFileName().toString().replaceFirst("\\.[^.]+$", ".noi"));
        try (CannonTest test = new CannonTest(rom, noi)) { test.run(); }
    }
}
