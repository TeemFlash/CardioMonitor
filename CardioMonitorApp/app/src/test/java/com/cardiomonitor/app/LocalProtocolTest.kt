package com.cardiomonitor.app

import com.cardiomonitor.app.data.importer.SdCsvReader
import com.cardiomonitor.app.data.network.readProtocolLine
import com.cardiomonitor.app.data.network.validArchiveName
import org.junit.Test
import org.junit.Assert.*
import java.io.ByteArrayInputStream
import java.io.EOFException
import java.io.IOException

class LocalProtocolTest {
    @Test fun readsAbsoluteStartWithoutChangingRelativeSampleTimes() {
        val csv = SdCsvReader(("millis_ms,ir,red,hr_valid,spo2_valid,correlation,bpm,spo2\n" +
            "# started_at_unix_ms=1791586800123\n1000,1,2,1,1,0.9,72,98\n").reader().buffered())
        assertEquals(1000L, csv.next()!!.ms)
        assertEquals(1791586800123L, csv.startedAtMs)
    }
    @Test fun lateClockSyncMetadataIsReadAtEndOfRecording() {
        val csv = SdCsvReader(("millis_ms,ir,red,hr_valid,spo2_valid,correlation,bpm,spo2\n" +
            "1000,1,2,1,1,0.9,72,98\n# started_at_unix_ms=1791586800123\n").reader().buffered())
        csv.next(); assertNull(csv.startedAtMs)
        assertNull(csv.next()); assertEquals(1791586800123L, csv.startedAtMs)
    }
    @Test fun invalidAndConflictingDatesCannotReplaceFirstValidDate() {
        val csv = SdCsvReader(("# started_at_unix_ms=0\n# started_at_unix_ms=garbage\n" +
            "# started_at_unix_ms=4102444800000\n# started_at_unix_ms=1791586800123\n" +
            "# started_at_unix_ms=1791586900123\n").reader().buffered())
        assertNull(csv.next()); assertEquals(1791586800123L, csv.startedAtMs)
    }
    @Test fun oldRecordHasNoInventedDate() {
        val csv = SdCsvReader("millis_ms,ir,red,hr_valid,spo2_valid,correlation,bpm,spo2\n1000,1,2,1,1,0.9,72,98\n".reader().buffered())
        csv.next(); csv.next(); assertNull(csv.startedAtMs)
    }
    @Test fun framingDoesNotEatBinaryPayload() {
        val input = ByteArrayInputStream("{header}\nabc\n{end}\n".toByteArray())
        assertEquals("{header}", readProtocolLine(input))
        assertEquals('a'.code, input.read()); assertEquals('b'.code, input.read()); assertEquals('c'.code, input.read())
        assertEquals("{end}", readProtocolLine(input))
    }
    @Test(expected = EOFException::class) fun truncatedLineFails() { readProtocolLine(ByteArrayInputStream("incomplete".toByteArray())) }
    @Test(expected = IOException::class) fun oversizedLineFails() { readProtocolLine(ByteArrayInputStream("x".repeat(8193).toByteArray())) }
    @Test fun rejectsTraversalAndNonRecordFiles() {
        assertTrue(validArchiveName("ecg_000000123.csv"))
        listOf("../ecg_1.csv", "/ecg_1.csv", "settings.csv", "ecg_1.csv\nGET secret", "pulse_.csv", "spo2_1234567890.csv").forEach { assertFalse(it, validArchiveName(it)) }
    }
    @Test fun oxygenColumnOrderAndValidity() {
        val csv = SdCsvReader(("# comment\nmillis_ms,ir,red,hr_valid,spo2_valid,correlation,bpm,spo2\n" +
            "0,42,43,1,1,0.98,75,98.5\n1000,42,43,0,0,0.98,75,98.5\n").reader().buffered())
        val first = csv.next()!!
        assertEquals(75, first.bpm); assertEquals(99, first.spo2); assertNull(first.ecg)
        val invalid = csv.next()!!; assertNull(invalid.bpm); assertNull(invalid.spo2)
        assertNull(csv.next())
    }
    @Test fun pulseColumnOrderAndMalformedRows() {
        val csv = SdCsvReader(("millis_ms,ir,red,hr_valid,spo2_valid,correlation,spo2,bpm\n" +
            "bad,row\n0,1,2,1,1,0.9,97,70\n1000,1,2,1,1,0.9,NaN,Infinity\n").reader().buffered())
        assertEquals(70, csv.next()!!.bpm); assertEquals(1, csv.damaged)
        val invalid = csv.next()!!; assertNull(invalid.bpm); assertNull(invalid.spo2)
    }
    @Test fun mixedEcgAndPpgAreNotInterchanged() {
        val csv = SdCsvReader(("millis_ms,kind,adc_code,filtered_relative,signal_ok,ir,red,ppg_valid,spo2_valid,correlation,ppg_bpm,spo2,ecg_bpm\n" +
            "4,ecg,123,1.25,1,,,,,,,,72\n8,ecg,123,NaN,0,,,,,,,,72\n10,ppg,,,,10,20,1,1,0.99,73,98,\n").reader().buffered())
        val ecg = csv.next()!!; assertEquals(1.25f, ecg.ecg); assertEquals(72, ecg.bpm)
        val bad = csv.next()!!; assertNull(bad.ecg); assertNull(bad.bpm)
        val ppg = csv.next()!!; assertNull(ppg.ecg); assertEquals(73, ppg.bpm); assertEquals(98, ppg.spo2)
    }
    @Test(expected = IllegalArgumentException::class) fun oversizedCsvLineFailsBeforeAllocation() {
        SdCsvReader(("#" + "x".repeat(5000)).reader().buffered()).next()
    }
    @Test(expected = IllegalArgumentException::class) fun unknownSchemaCannotBecomeValidVitals() {
        SdCsvReader("millis_ms,bpm\n0,70\n".reader().buffered()).next()
    }
}
