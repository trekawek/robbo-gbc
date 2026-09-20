/* Isolated production-engine SFX capture/regression using Coffee GB core.
 * Build tools/sound_test.c with src/sound.c and matching linker symbols:
 *
 * third_party/gbdk/bin/lcc -Wm-yc -Wl-yt0x1B -Wl-yo4 -Wl-ya4 -DCGB \
 *   -Isrc -Wf--opt-code-size -Wl-j -autobank -o build/sound-test.gbc \
 *   tools/sound_test.c tools/sound_test_bank.c build/sound.o
 * java --class-path "$SOUND_TEST_CP" tools/SoundTest.java \
 *   build/sound-test.gbc build/sound-captures /path/to/atari/robbo/d1/R1.ASM
 *
 * SOUND_TEST_CP contains a built Coffee GB core and its dependency JARs.
 * Output: 44.1 kHz stereo WAV and frame-by-frame APU register CSV per effect.
 * The CSV uses the debugger's actual registers, because frequency registers
 * such as NR13 are write-only and cannot be measured through CPU reads.
 * active_mask means audible channels; nr52 and chN_active retain hardware
 * enable flags, which can remain set while a DAC is muted to avoid clicks.
 * The optional Atari source enables pitch/envelope/cadence and scheduling
 * checks; omit it to record an older engine for a before/after comparison.
 */
import eu.rekawek.coffeegb.core.Gameboy;
import eu.rekawek.coffeegb.core.GameboyType;
import eu.rekawek.coffeegb.core.debug.DebugAudioInspection;
import eu.rekawek.coffeegb.core.events.EventBus;
import eu.rekawek.coffeegb.core.events.EventBusImpl;
import eu.rekawek.coffeegb.core.serial.SerialEndpoint;
import eu.rekawek.coffeegb.core.sound.Sound;
import eu.rekawek.coffeegb.core.sound.StereoPcmConverter;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.regex.Pattern;
import javax.sound.sampled.AudioFileFormat;
import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.AudioInputStream;
import javax.sound.sampled.AudioSystem;

public class SoundTest {
    private static final String[] NAMES = {
        "explosion", "shot", "knock", "teleport", "screw", "life", "door", "ammo",
        "push", "key", "destroy", "enter", "win", "capsule", "magnet"
    };
    private static final int RATE = 44_100, CAPTURE_FRAMES = 110;
    private static final double FRAME_MS = 1000.0 * 70_224 / 4_194_304;
    private static final double POKEY_CLOCK = 1_773_447.0;
    private static final double STEP_MS = 4 * 1000.0 * 114 * 312 / POKEY_CLOCK;
    private final EventBus bus = new EventBusImpl();
    private final Gameboy gb;
    private final int command, id, ready, frames, pause, bankResult;
    private final Path directory;
    private Path captureDirectory;
    private final int[][] reference;
    private final StereoPcmConverter converter = new StereoPcmConverter(RATE);
    private ByteArrayOutputStream pcm;
    private byte[] pcmBuffer = new byte[8_192];

    private SoundTest(Path rom, Path directory, Path atariSource) throws Exception {
        this.directory = directory;
        captureDirectory = directory;
        reference = atariSource == null ? null : reference(atariSource);
        Files.createDirectories(directory);
        Path noi = rom.resolveSibling(rom.getFileName().toString().replaceFirst("\\.[^.]+$", ".noi"));
        Map<String, Integer> symbols = new HashMap<>();
        Pattern pattern = Pattern.compile("^DEF\\s+(\\S+)\\s+0x([0-9a-fA-F]+)$");
        for (String line : Files.readAllLines(noi)) {
            var match = pattern.matcher(line);
            if (match.matches()) symbols.put(match.group(1), Integer.parseInt(match.group(2), 16));
        }
        command = symbol(symbols, "_sound_test_command");
        id = symbol(symbols, "_sound_test_id");
        ready = symbol(symbols, "_sound_test_ready");
        frames = symbol(symbols, "_sound_test_frames");
        pause = symbol(symbols, "_sound_test_pause");
        bankResult = symbol(symbols, "_sound_test_bank_result");
        gb = new Gameboy.GameboyConfiguration(rom.toFile())
                .setBootstrapMode(Gameboy.BootstrapMode.SKIP)
                .setGameboyType(GameboyType.CGB).setSupportBatterySave(false).build();
        bus.register(this::samples, Sound.SoundSampleEvent.class);
        gb.init(bus, SerialEndpoint.NULL_ENDPOINT, null);
        long budget = 5_000_000;
        while (read(ready) != 0xa5 && budget-- > 0) gb.tick();
        check(read(ready) == 0xa5, "test ROM never became ready; check matching .noi");
        frame();
    }

    private static int symbol(Map<String, Integer> symbols, String name) {
        Integer address = symbols.get(name);
        if (address == null) throw new IllegalArgumentException("Missing build symbol " + name);
        return address;
    }

    private static int[][] reference(Path source) throws Exception {
        int[][] result = new int[NAMES.length][16];
        Pattern word = Pattern.compile("DTA\\s+A\\(\\$([0-9a-fA-F]{4})\\)");
        boolean reading = false;
        int count = 0;
        for (String line : Files.readAllLines(source, java.nio.charset.StandardCharsets.ISO_8859_1)) {
            if (line.matches("\\s*TABS\\s+EQU.*")) reading = true;
            if (!reading) continue;
            var match = word.matcher(line);
            if (match.find()) {
                // Atari's count runs down from 16, so the source table is reversed.
                result[count / 16][15 - count % 16] = Integer.parseInt(match.group(1), 16);
                if (++count == NAMES.length * 16) return result;
            }
        }
        throw new IllegalArgumentException("Incomplete Atari TABS in " + source);
    }

    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private int read(int address) { return gb.getAddressSpace().getByte(address) & 255; }
    private void write(int address, int value) { gb.getAddressSpace().setByte(address, value & 255); }

    private void samples(Sound.SoundSampleEvent event) {
        if (pcm == null) return;
        int required = converter.maximumPcmBytes(event.buffer().length / 2, event.clockSpec());
        if (pcmBuffer.length < required) pcmBuffer = new byte[required];
        int size = converter.render(event.buffer(), event.clockSpec(), 100, false, pcmBuffer);
        pcm.write(pcmBuffer, 0, size);
    }

    private void frame() {
        int before = read(frames);
        int budget = 200_000;
        while (read(frames) == before && budget-- > 0) gb.tick();
        check(read(frames) != before, "sound test main loop stalled");
    }

    private void frames(int count) { for (int i = 0; i < count; i++) frame(); }

    private void command(int value, int sound) {
        write(id, sound);
        write(command, value);
        frame();
        check(read(command) == 0, "test command was not acknowledged");
    }

    private int activeChannels() { return audibleMask(audio()); }
    private DebugAudioInspection audio() { return gb.getSound().captureDebugAudioInspection(); }

    private static int audibleMask(DebugAudioInspection audio) {
        int mask = 0;
        for (var channel : audio.channels()) {
            // These SFX use a constant envelope (pace zero), so the programmed
            // volume also describes the running envelope. A live, zero-volume
            // DAC is intentional and is not an audible channel.
            int volume = channel.channel() == 3 ? channel.nr2() & 0x60 : channel.nr2() >> 4;
            if (channel.enabled() && volume != 0) mask |= 1 << (channel.channel() - 1);
        }
        return mask;
    }

    private static void csvRow(StringBuilder csv, int frame, DebugAudioInspection audio) {
        csv.append(frame).append(',').append(String.format(Locale.ROOT, "%.3f", frame * FRAME_MS));
        csv.append(',').append(audibleMask(audio)).append(',').append(audio.nr52());
        for (var ch : audio.channels()) {
            csv.append(',').append(ch.enabled() ? 1 : 0)
                    .append(',').append(ch.nr0()).append(',').append(ch.nr1())
                    .append(',').append(ch.nr2()).append(',').append(ch.nr3())
                    .append(',').append(ch.nr4());
        }
        csv.append('\n');
    }

    private void capture(int sound) throws Exception {
        command(2, 0);
        frames(3);
        converter.reset();
        pcm = new ByteArrayOutputStream();
        frames(12); // Settle the output high-pass filter at the idle DAC bias.
        pcm.reset();
        frames(3); // Retain a short silence before the onset for listening.
        command(1, sound);
        StringBuilder csv = new StringBuilder("frame,ms,active_mask,nr52");
        for (int ch = 1; ch <= 4; ch++) {
            csv.append(",ch").append(ch).append("_active");
            for (int reg = 0; reg <= 4; reg++) csv.append(",nr").append(ch).append(reg);
        }
        csv.append('\n');
        int firstActive = -1, lastActive = -1, activeFrames = 0, channelMask = 0;
        List<DebugAudioInspection> states = new ArrayList<>();
        for (int i = 0; i < CAPTURE_FRAMES; i++) {
            var state = audio();
            int active = audibleMask(state);
            states.add(state);
            csvRow(csv, i, state);
            if (active != 0) {
                if (firstActive < 0) firstActive = i;
                lastActive = i;
                activeFrames++;
                channelMask |= active;
            }
            frame();
        }
        byte[] bytes = pcm.toByteArray();
        pcm = null;
        String basename = String.format(Locale.ROOT, "%02d-%s", sound, NAMES[sound]);
        Files.writeString(captureDirectory.resolve(basename + ".csv"), csv);
        AudioFormat format = new AudioFormat(RATE, 16, 2, true, false);
        try (AudioInputStream input = new AudioInputStream(
                new ByteArrayInputStream(bytes), format, bytes.length / 4)) {
            AudioSystem.write(input, AudioFileFormat.Type.WAVE, captureDirectory.resolve(basename + ".wav").toFile());
        }
        long squares = 0;
        for (int i = 0; i < bytes.length; i += 2) {
            int sample = (short) ((bytes[i] & 255) | bytes[i + 1] << 8);
            squares += (long) sample * sample;
        }
        double rms = Math.sqrt(squares / (bytes.length / 2.0));
        check(firstActive >= 0 && rms > 1, NAMES[sound] + " did not produce audible output");
        check(lastActive < CAPTURE_FRAMES - 5, NAMES[sound] + " did not finish");
        check(activeChannels() == 0, NAMES[sound] + " left a channel active");
        if (reference != null) checkReference(sound, states, firstActive, lastActive);
        System.out.printf(Locale.ROOT,
                "PASS %-10s active=%3d frames span=%7.2f ms channels=0x%x rms=%7.2f%n",
                NAMES[sound], activeFrames, (lastActive - firstActive + 1) * FRAME_MS, channelMask, rms);
    }

    private void checkReference(int sound, List<DebugAudioInspection> states,
                                int firstActive, int lastActive) {
        int span = 0;
        for (int step = 0; step < 16; step++) if ((reference[sound][step] & 15) != 0) span = step + 1;
        check(firstActive <= 5, NAMES[sound] + " onset exceeds one Atari sound tick");
        check(Math.abs((lastActive - firstActive + 1) * FRAME_MS - span * STEP_MS) <= FRAME_MS * 1.1,
                NAMES[sound] + " duration does not match Atari PAL timing");
        for (int frame = firstActive; frame < states.size(); frame++) {
            // The first audible frame brackets the exact onset within one VBlank.
            int firstStep = (int) ((frame - firstActive) * FRAME_MS / STEP_MS);
            int lastStep = (int) ((frame - firstActive + 1) * FRAME_MS / STEP_MS);
            boolean matched = false;
            for (int step = firstStep; step <= lastStep; step++) {
                int word = step < 16 ? reference[sound][step] : 0;
                matched |= matches(states.get(frame), word);
            }
            check(matched, NAMES[sound] + " disagrees with Atari waveform/volume/pitch at frame "
                    + frame + " (reference step " + firstStep + ".." + lastStep + ")");
        }
    }

    private static boolean matches(DebugAudioInspection state, int word) {
        int volume = word & 15, distortion = (word >> 5) & 7, divisor = word >> 8;
        int active = audibleMask(state);
        if (volume == 0) return active == 0;
        if (distortion == 5) {
            if (active != 1) return false;
            var channel = state.channels().get(0);
            double actual = 131_072.0 / (2048 - (channel.nr3() | (channel.nr4() & 7) << 8));
            double expected = POKEY_CLOCK / (28 * 2 * (divisor + 1));
            return (channel.nr2() >> 4) == volume && Math.abs(actual / expected - 1) < 0.012;
        }
        if (distortion == 6) {
            if (active != 4) return false;
            var channel = state.channels().get(2);
            int stride = 28 * (divisor + 1), gcd = stride, remainder = 15;
            while (remainder != 0) {
                int next = gcd % remainder;
                gcd = remainder;
                remainder = next;
            }
            // POKEY's free-running poly4 is sampled by AUDF, so some divisors
            // select a repeating subset of the 15-bit pattern.
            double expected = Math.max(32, POKEY_CLOCK / (stride * (15 / gcd)));
            double actual = 65_536.0 / (2048 - (channel.nr3() | (channel.nr4() & 7) << 8));
            int peak = 0;
            for (int i = 0; i < 16; i++) {
                int value = state.waveRam().unsignedByteAt(i);
                peak = Math.max(peak, Math.max(value >> 4, value & 15));
            }
            return peak == volume && Math.abs(actual / expected - 1) < 0.012;
        }
        if (active != 8 || (state.channels().get(3).nr2() >> 4) != volume) return false;
        int polynomial = state.channels().get(3).nr3();
        if ((polynomial & 8) != 0) return false; // Long noise, not the tonal 7-bit mode.
        double expected = POKEY_CLOCK / (28 * (divisor + 1));
        if (distortion == 0) expected *= 15.0 / 31;
        double error = Math.abs(Math.log(noiseClock(polynomial) / expected));
        // No available long-LFSR divisor may give a closer transition rate.
        for (int shift = 0; shift < 14; shift++) {
            for (int ratio = 0; ratio < 8; ratio++) {
                if (Math.abs(Math.log(noiseClock(shift << 4 | ratio) / expected)) + 1e-9 < error) {
                    return false;
                }
            }
        }
        return true;
    }

    private static double noiseClock(int polynomial) {
        int ratio = polynomial & 7;
        int divider = ratio == 0 ? 8 : ratio * 16;
        return 4_194_304.0 / (divider << (polynomial >> 4));
    }

    private void lifecycle() {
        command(1, 5);
        frames(6);
        check(activeChannels() != 0, "life did not start for stop test");
        command(2, 0);
        check(activeChannels() == 0, "snd_stop did not silence all channels");
        settledSilence();
        frames(90);
        check(activeChannels() == 0, "stopped effect resumed");
        command(1, 255);
        check(activeChannels() == 0, "invalid SFX id started a channel");
        System.out.println("PASS stop cancels playback with silent PCM; invalid ids remain silent");
    }

    private void settledSilence() {
        converter.reset();
        pcm = new ByteArrayOutputStream();
        frames(12);
        pcm.reset();
        frames(4);
        byte[] samples = pcm.toByteArray();
        pcm = null;
        check(samples.length > 0, "no PCM was captured after stopping");
        int peak = 0;
        for (int i = 0; i < samples.length; i += 2) {
            int sample = (short) ((samples[i] & 255) | samples[i + 1] << 8);
            peak = Math.max(peak, Math.abs(sample));
        }
        check(peak <= 1, "snd_stop left audible PCM after settling: peak=" + peak);
    }

    private void originalScheduling() {
        command(2, 0);
        command(1, 5); // Life on Atari's shared voice.
        frames(6);
        command(1, 0); // Explosion has an independent Atari voice.
        frames(6);
        check((activeChannels() & 9) == 9, "explosion cut off the life pickup");
        command(1, 1);
        frames(6);
        check((activeChannels() & 9) == 9, "shooting cut off an existing pickup/explosion");
        command(2, 0);
        command(1, 5);
        frames(6);
        command(1, 8); // Push replaces life; original has no pickup-priority filter.
        frames(6);
        check(activeChannels() == 8, "a new shared effect did not replace the life pickup");
        frames(20);
        check(activeChannels() == 0, "replaced life pickup resumed after push");
        write(pause, 1); // No snd_update calls while the effect runs.
        command(1, 5);
        frames(6);
        check(activeChannels() == 1, "playback depended on snd_update to start");
        frames(80);
        check(activeChannels() == 0, "playback stopped advancing without snd_update");
        write(pause, 0);
        command(1, 5);
        write(command, 4); // Stay in a different bank for 90 VBlanks.
        gb.runTicks(10L * 70_224);
        check(read(command) == 4 && activeChannels() == 1,
                "banked workload did not run alongside sound");
        gb.runTicks(90L * 70_224);
        check(read(command) == 0 && read(bankResult) == 0xa5,
                "sound interrupt did not restore the interrupted ROM bank");
        check(activeChannels() == 0, "sound did not finish during the banked workload");
        System.out.println("PASS independent Atari voices, shared replacement, interrupt timing and bank restore");
    }

    private void run() throws Exception {
        for (int sound = 0; sound < NAMES.length; sound++) capture(sound);
        lifecycle();
        if (reference != null) {
            originalScheduling();
            command(5, 0);
            check((read(0xff4d) & 128) != 0, "CGB did not enter double speed");
            captureDirectory = directory.resolve("double-speed");
            Files.createDirectories(captureDirectory);
            System.out.println("Checking all effects again at CGB double CPU speed:");
            for (int sound = 0; sound < NAMES.length; sound++) capture(sound);
            lifecycle();
            originalScheduling();
        }
        System.out.println("SFX capture and regression passed: " + directory);
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 1 || args.length > 3) {
            throw new IllegalArgumentException("Usage: SoundTest sound-test.gbc [capture-directory] [Atari R1.ASM]");
        }
        Path rom = Path.of(args[0]).toAbsolutePath();
        Path directory = args.length >= 2 ? Path.of(args[1]).toAbsolutePath()
                : rom.resolveSibling("sound-captures");
        SoundTest test = new SoundTest(rom, directory, args.length == 3 ? Path.of(args[2]) : null);
        try { test.run(); } finally { test.gb.close(); test.bus.close(); }
    }
}
