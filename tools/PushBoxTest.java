/* Level 29 striped sliding-box regression (Coffee GB core).
 *
 * java --class-path "$PUSH_BOX_TEST_CP" tools/PushBoxTest.java ROM.gbc [ROM.noi]
 * Matching linker symbols are required; defaults to the ROM's .noi sidecar.
 *
 * Reproduces the starting-box push using real directional input on the loaded
 * board. Isolated fixtures relocate the stopped box and Robbo to check another
 * axis, then place Robbo beside the lower box to check its impact on a bomb.
 * Active-row bookkeeping is checked at every gameplay update boundary.
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
public class PushBoxTest implements AutoCloseable {
    private static final int WIDTH = 16, HEIGHT = 31, CELL_BYTES = 14;
    private static final int EMPTY = 0, WALL = 2, BOMB = 8, PUSH_BOX = 60;
    private static final int SOUTH = 1, WEST = 2;
    private final EventBus bus = new EventBusImpl();
    private final Gameboy gb;
    private final Map<String, Integer> symbols = new HashMap<>();
    private final int board, robbo, currentBank, update;
    private long ticks;
    private int updates;

    private PushBoxTest(Path rom, Path noi) throws Exception {
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
    private void putWord(int address, int value) { put(address, value); put(address + 1, value >> 8); }
    private int cell(int x, int y) { return board + (x * HEIGHT + y) * CELL_BYTES; }
    private void check(boolean condition, String message) {
        if (!condition) throw new AssertionError("Level 29: " + message);
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
        until(() -> !at(address), "leaving function entry");
        until(() -> at(address), "function " + Integer.toHexString(address));
    }
    private void runTicks(int count) { gb.runTicks(count); ticks += count; }
    private void step() {
        next(update);
        updates++;
        assertRows();
        check(w(robbo + 4) != 0 && w(symbol("_restart_timeout")) == 0,
                "Robbo died or restarted at update " + updates);
        check(w(symbol("_level_packs") + 4) == 29, "level changed");
    }
    private void boot() {
        runTicks(5_000_000);
        bus.post(new ButtonPressEvent(Button.START));
        runTicks(1_200_000);
        bus.post(new ButtonReleaseEvent(Button.START));
        next(symbol("_level_init"));
        putWord(symbol("_level_packs") + 4, 29);
        next(update);
        check(w(symbol("_level")) == WIDTH && w(symbol("_level") + 2) == HEIGHT,
                "unexpected board dimensions or mismatched symbols");
        check(w(robbo) == 13 && w(robbo + 2) == 2, "unexpected starting position");
        assertRows();
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
    private void assertBoxTile(int x, int y) {
        int map = (b(0xff40) & 8) != 0 ? 0x9c00 : 0x9800;
        int[] expected = {0x4e, 0x4f, 0x6e, 0x6f};
        for (int q = 0; q < 4; q++) {
            int address = map + (y & 15) * 64 + x * 2 + (q & 1) + (q >> 1) * 32;
            // Direct VRAM access avoids legitimate CPU-bus lockout during LCD transfer.
            int tile = gb.getGpu().getVideoRam0().getByte(address);
            int palette = gb.getGpu().getVideoRam1().getByte(address) & 7;
            check(tile == expected[q] && palette == 0, "striped box at (" + x + "," + y
                    + ") quadrant " + q + " renders tile " + tile + ", palette " + palette);
        }
    }
    private int boxX() {
        int found = -1;
        for (int x = 0; x < WIDTH; x++) if (b(cell(x, 2)) == PUSH_BOX) {
            check(found == -1, "duplicated starting box");
            found = x;
        }
        check(found >= 0, "starting box disappeared");
        return found;
    }

    private int startingBox() {
        check(b(cell(11, 2)) == PUSH_BOX, "starting striped box is type "
                + b(cell(11, 2)) + ", expected PUSH_BOX (60)");
        check(b(cell(13, 27)) == PUSH_BOX, "lower striped box is not PUSH_BOX");
        assertBoxTile(11, 2);
        bus.post(new ButtonPressEvent(Button.LEFT));
        for (int i = 0; w(robbo) != 11 && i < 12; i++) step();
        bus.post(new ButtonReleaseEvent(Button.LEFT));
        check(w(robbo) == 11 && w(robbo + 2) == 2, "left input could not push the starting box");
        check(boxX() == 10 && b(cell(10, 2) + 2) == 1
                && b(cell(10, 2) + 3) == WEST, "initial push did not start westward sliding");
        int previousX = 10;
        for (int i = 0; i < 48; i++) {
            step();
            int x = boxX();
            check(x == previousX || x == previousX - 1, "box skipped a cell or reversed");
            check(w(robbo) == 11 && w(robbo + 2) == 2, "Robbo moved after button release");
            assertBoxTile(x, 2);
            previousX = x;
            if (b(cell(x, 2) + 2) == 0) break;
        }
        int x = boxX();
        check(x < 10 && b(cell(x, 2) + 2) == 0, "box did not slide and stop after release");
        for (int i = 0; i < 6; i++) {
            step();
            check(boxX() == x && b(cell(x, 2) + 2) == 0, "stopped box restarted without a push");
        }
        check(b(cell(0, 2)) == WALL, "sliding box damaged the outer wall");
        System.out.printf("Loaded Level 29: left input pushes striped box from (11,2); it slides to (%d,2) after release%n", x);
        return x;
    }

    private void secondAxis(int oldX) {
        // Isolated fixture: relocate the same stopped box and Robbo within
        // the cleared starting area. The destination corridor is unchanged.
        for (int y = 1; y <= 3; y++)
            check(b(cell(4, y)) == EMPTY, "fixture corridor is not empty at (4," + y + ")");
        check(b(cell(4, 4)) == WALL, "fixture stopping wall is missing");
        int source = cell(oldX, 2), target = cell(4, 2);
        check((b(source + 13) & 4) != 0 && (b(target + 13) & 4) == 0,
                "unexpected fixture activity flags");
        for (int field = 0; field < CELL_BYTES; field++) put(target + field, b(source + field));
        for (int field = 0; field < CELL_BYTES; field++) put(source + field, 0);
        put(source + 11, w(symbol("_cycle_count")));
        put(source + 12, 2); // empty-field blowable flag
        put(source + 13, 2); // empty-field redraw flag
        put(target + 13, b(target + 13) | 2);
        // Both fixture cells belong to row 2, preserving its active count.
        putWord(robbo, 4);
        putWord(robbo + 2, 1);
        putWord(robbo + 16, 0);
        assertRows();
        bus.post(new ButtonPressEvent(Button.DOWN));
        for (int i = 0; w(robbo + 2) != 2 && i < 8; i++) step();
        bus.post(new ButtonReleaseEvent(Button.DOWN));
        check(w(robbo) == 4 && w(robbo + 2) == 2, "stopped box could not be pushed on a new axis");
        check(b(cell(4, 3)) == PUSH_BOX && b(cell(4, 3) + 3) == SOUTH,
                "second push did not change direction to south");
        for (int i = 0; i < 8; i++) step();
        check(b(cell(4, 3)) == PUSH_BOX && b(cell(4, 3) + 2) == 0,
                "southbound box did not stop at the wall");
        check(b(cell(4, 4)) == WALL, "southbound box damaged its stopping wall");
        check(b(cell(4, 2)) == EMPTY, "second push left a duplicated box");
        System.out.println("Relocation fixture: stopped box can be pushed south across rows and stop at a wall");
    }

    private void lowerBox() {
        // Isolated fixture: place Robbo beside the authentic lower box. Its
        // corridor, bombs, and all other objects retain their live level state.
        check(b(cell(14, 27)) == EMPTY && b(cell(13, 27)) == PUSH_BOX,
                "lower-box fixture no longer matches the loaded level");
        check(b(cell(7, 27)) == BOMB, "expected bomb at the end of the lower corridor");
        putWord(robbo, 14);
        putWord(robbo + 2, 27);
        putWord(robbo + 16, 0);
        bus.post(new ButtonPressEvent(Button.LEFT));
        for (int i = 0; w(robbo) != 13 && i < 8; i++) step();
        bus.post(new ButtonReleaseEvent(Button.LEFT));
        check(b(cell(12, 27)) == PUSH_BOX && b(cell(12, 27) + 2) == 1,
                "lower box could not be pushed west");
        // Retreat through the existing passage using normal input while the
        // box continues toward the bomb; no board cells are modified.
        bus.post(new ButtonPressEvent(Button.RIGHT));
        for (int i = 0; w(robbo) != 14 && i < 8; i++) step();
        bus.post(new ButtonReleaseEvent(Button.RIGHT));
        check(w(robbo) == 14, "could not retreat east after pushing lower box");
        bus.post(new ButtonPressEvent(Button.UP));
        for (int i = 0; w(robbo + 2) != 24 && i < 12; i++) step();
        bus.post(new ButtonReleaseEvent(Button.UP));
        check(w(robbo + 2) == 24, "could not retreat north before impact");
        boolean sawImpact = false;
        for (int i = 0; i < 16; i++) {
            step();
            if (b(cell(8, 27)) == PUSH_BOX && b(cell(8, 27) + 2) == 0
                    && b(cell(7, 27)) != BOMB) sawImpact = true;
        }
        check(sawImpact, "lower box did not stop and trigger its blocking bomb");
        System.out.println("Lower-box fixture: westbound box triggers blocking bomb; Robbo retreats safely");
    }

    private void run() {
        boot();
        secondAxis(startingBox());
        lowerBox();
        System.out.printf("PASS %d live updates; authentic striped tiles and palette; exact active-row counts at every boundary%n", updates);
    }
    @Override public void close() { try { gb.close(); } finally { bus.close(); } }
    public static void main(String[] args) throws Exception {
        if (args.length < 1 || args.length > 2)
            throw new IllegalArgumentException("Usage: PushBoxTest ROM.gbc [ROM.noi]");
        Path rom = Path.of(args[0]).toAbsolutePath();
        Path noi = args.length > 1 ? Path.of(args[1])
                : rom.resolveSibling(rom.getFileName().toString().replaceFirst("\\.[^.]+$", ".noi"));
        try (PushBoxTest test = new PushBoxTest(rom, noi)) { test.run(); }
    }
}
