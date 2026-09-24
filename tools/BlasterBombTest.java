/* Authentic level 38: a left-facing blaster must detonate the bomb behind
 * its blocking box. Remove only that box in RAM to isolate the interaction.
 *
 * java --class-path "$CANNON_TEST_CP" tools/BlasterBombTest.java ROM.gbc ROM.noi
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
public class BlasterBombTest implements AutoCloseable {
    private static final int EMPTY = 0, BOX = 6, BOMB = 8, BIG_BOOM = 42;
    private static final int GUN = 50, BLASTER = 58;
    private final EventBus bus = new EventBusImpl();
    private final Gameboy gb;
    private final Map<String, Integer> symbols = new HashMap<>();
    private final int board, currentBank, update;
    private long ticks;

    private BlasterBombTest(Path rom, Path noi) throws Exception {
        var pattern = Pattern.compile("^DEF\\s+(\\S+)\\s+0x([0-9a-fA-F]+)$");
        for (String line : Files.readAllLines(noi)) {
            var match = pattern.matcher(line);
            if (match.matches())
                symbols.put(match.group(1), Integer.parseInt(match.group(2), 16));
        }
        board = symbol("_board");
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

    private void run(boolean adjacent) {
        gb.runTicks(5_000_000);
        ticks += 5_000_000;
        bus.post(new ButtonPressEvent(Button.START));
        gb.runTicks(1_200_000);
        ticks += 1_200_000;
        bus.post(new ButtonReleaseEvent(Button.START));
        next(symbol("_level_init"));
        put(symbol("_level_packs") + 4, 38);
        next(update);

        check(b(cell(15, 14)) == GUN && b(cell(15, 14) + 3) == 2
                        && b(cell(15, 14) + 7) == 2,
                "expected west-facing blaster cannon in level 38");
        check(b(cell(12, 14)) == BOMB && b(cell(13, 14)) == BOX
                        && b(cell(14, 14)) == EMPTY,
                "expected bomb, box, and firing lane in level 38");

        int targetX;
        if (adjacent) {
            // Move the authentic, inert bomb next to the same gun to cover
            // direct muzzle contact as well as projectile-head contact.
            int source = cell(12, 14), target = cell(14, 14);
            check((b(source + 13) & 4) == 0, "expected an inactive bomb");
            for (int field = 0; field < 14; field++) put(target + field, b(source + field));
            for (int field = 0; field < 14; field++) put(source + field, 0);
            put(source + 11, b(symbol("_cycle_count")));
            put(source + 12, 2);
            targetX = 14;
        } else {
            // The box is inert. Replacing it with an inert empty cell preserves
            // the active-row count; the gun and bomb remain live.
            int blocker = cell(13, 14);
            for (int field = 0; field < 14; field++) put(blocker + field, 0);
            put(blocker + 11, b(symbol("_cycle_count")));
            put(blocker + 12, 2);
            targetX = 12;
        }

        boolean sawBlaster = false, sawExplosion = false;
        for (int step = 0; step < 200; step++) {
            next(update);
            if (b(cell(13, 14)) == BLASTER) sawBlaster = true;
            if (b(cell(targetX, 14)) != BOMB) {
                sawExplosion = true;
                check(b(cell(targetX, 14)) == BIG_BOOM, "bomb did not become an explosion");
                break;
            }
        }
        if (!adjacent) check(sawBlaster, "blaster never reached the bomb");
        check(sawExplosion, adjacent
                ? "direct blaster hit never triggered the bomb"
                : "blaster reached the bomb but never triggered it");
        System.out.println("PASS level 38 blaster triggers bomb "
                + (adjacent ? "at the muzzle" : "after its projectile reaches it"));
    }

    @Override public void close() { try { gb.close(); } finally { bus.close(); } }
    public static void main(String[] args) throws Exception {
        if (args.length != 2)
            throw new IllegalArgumentException("Usage: BlasterBombTest ROM.gbc ROM.noi");
        for (boolean adjacent : new boolean[] {false, true}) {
            try (BlasterBombTest test = new BlasterBombTest(Path.of(args[0]), Path.of(args[1]))) {
                test.run(adjacent);
            }
        }
    }
}
