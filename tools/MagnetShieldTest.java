/* Level 56 upper-right bear and magnet timing (Coffee GB core).
 *
 * java --class-path "$TIMING_TEST_CP" tools/MagnetShieldTest.java ROM.gbc ROM.noi
 * The level, magnet, and bear are authentic; only their positions are changed
 * in emulator RAM to expose the short shielding window deterministically.
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
public class MagnetShieldTest implements AutoCloseable {
    private static final int EMPTY = 0, BEAR = 11, MAGNET = 54;
    private final EventBus bus = new EventBusImpl();
    private final Gameboy gb;
    private final Map<String, Integer> symbols = new HashMap<>();
    private final int board, robbo, currentBank, update;
    private long ticks;

    private MagnetShieldTest(Path rom, Path noi) throws Exception {
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
    private void put(int address, int value) { gb.getAddressSpace().setByte(address, value & 255); }
    private int word(int address) { return b(address) | b(address + 1) << 8; }
    private void putWord(int address, int value) {
        put(address, value);
        put(address + 1, value >> 8);
    }
    private int cell(int x, int y) { return board + (x * 31 + y) * 14; }
    private static void check(boolean ok, String message) {
        if (!ok) throw new AssertionError(message);
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

    private void setUpShield() {
        gb.runTicks(5_000_000);
        ticks += 5_000_000;
        bus.post(new ButtonPressEvent(Button.START));
        gb.runTicks(1_200_000);
        ticks += 1_200_000;
        bus.post(new ButtonReleaseEvent(Button.START));
        next(symbol("_level_init"));
        putWord(symbol("_level_packs") + 4, 56);
        next(update);

        int source = cell(9, 3), shield = cell(12, 4), magnet = cell(11, 4);
        check(b(source) == BEAR && b(shield) == EMPTY
                        && b(magnet) == MAGNET && b(magnet + 3) == 0,
                "level 56 upper-right magnet and bear changed");
        check((b(source + 13) & 4) != 0, "bear is not active");
        for (int field = 0; field < 14; field++) {
            put(shield + field, b(source + field));
            put(source + field, 0);
        }
        put(source + 11, b(symbol("_cycle_count")));
        put(source + 12, 2); // inert empty cell
        put(shield + 3, 3); // north: west is occupied by the magnet
        put(shield + 8, 1); // moves off the magnet's row on its next update
        int rows = symbol("_gr_row_active");
        put(rows + 3, b(rows + 3) - 1);
        put(rows + 4, b(rows + 4) + 1);
        putWord(robbo, 14);
        putWord(robbo + 2, 4);
        putWord(robbo + 16, 0); // ready to move when the player presses UP
        putWord(robbo + 22, 0);

        for (int i = 0; b(shield) == BEAR && i < 4; i++) next(update);
        check(b(shield) == EMPTY && b(cell(12, 3)) == BEAR,
                "bear did not leave the magnet's line of sight");
        check(word(robbo + 22) == 0, "magnet caught Robbo through the bear");
    }

    private void captureCadence() {
        setUpShield();
        next(update); // intervening half step
        check(word(robbo + 22) == 0, "magnet scanned before the next Atari step");
        next(update); // next full scan
        check(word(robbo + 22) == 1 && word(robbo + 24) == 2,
                "magnet failed to capture exposed Robbo on its next scan");
        check(word(robbo + 16) == 4, "magnetic pull did not wait two Atari steps");
        for (int i = 0; i < 3; i++) {
            next(update);
            check(word(robbo) == 14, "magnet pulled Robbo too soon");
        }
        next(update);
        check(word(robbo) == 13, "magnet missed the first pull");
        for (int i = 0; i < 3; i++) {
            next(update);
            check(word(robbo) == 13, "magnet pulled Robbo faster than Atari");
        }
        next(update);
        check(word(robbo) == 12, "magnet missed the second pull");
        System.out.println("PASS level 56 magnet scans every two ticks and pulls every four");
    }

    private void lateEscape() {
        setUpShield();
        // Press after the bear has left. The former GBC magnet caught Robbo
        // during this half step, before the next input sample could move him.
        bus.post(new ButtonPressEvent(Button.UP));
        next(update);
        check(word(robbo + 2) == 3 && word(robbo + 22) == 0,
                "Robbo could not use the monster-shielding half step to escape");
        bus.post(new ButtonReleaseEvent(Button.UP));
        next(update);
        check(word(robbo + 4) == 1 && word(robbo + 22) == 0,
                "magnet captured Robbo after he left its row");
        System.out.println("PASS level 56 Robbo can leave after the bear uncovers him");
    }

    @Override public void close() { try { gb.close(); } finally { bus.close(); } }
    public static void main(String[] args) throws Exception {
        if (args.length != 2)
            throw new IllegalArgumentException("Usage: MagnetShieldTest ROM.gbc ROM.noi");
        try (MagnetShieldTest test = new MagnetShieldTest(Path.of(args[0]), Path.of(args[1]))) {
            test.captureCadence();
        }
        try (MagnetShieldTest test = new MagnetShieldTest(Path.of(args[0]), Path.of(args[1]))) {
            test.lateEscape();
        }
    }
}
