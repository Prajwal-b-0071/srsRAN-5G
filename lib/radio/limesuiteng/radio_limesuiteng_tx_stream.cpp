#include "radio_limesuiteng_tx_stream.h"
#include "limesuiteng/LimePlugin.h"
#include "limesuiteng/StreamMeta.h"
#include "limesuiteng/complex.h"
#include "srsran/radio/radio_constants.h"

using namespace srsran;

radio_limesuiteng_tx_stream::radio_limesuiteng_tx_stream(std::shared_ptr<LimePluginContext> ctx,
                                                         uint8_t                            id,
                                                         radio_event_notifier&              notifier_) :
  context(ctx), portId(id), notifier(notifier_), do_work(false)
{
}

// See interface for documentation.
void radio_limesuiteng_tx_stream::transmit(const baseband_gateway_buffer_reader&        data,
                                           const baseband_gateway_transmitter_metadata& inmeta)
{
  if (!do_work)
    return;

  // Empty buffers are zero-filled by the lower PHY and are still written, as the UHD driver does in continuous mode.
  // LimeSuiteNG packs consecutive writes into the same FPGA packet using the timestamp of the first one, so skipping
  // empty buffers merges the tail of a burst into the first packet of the next burst with a stale timestamp.
  unsigned start_padding = inmeta.tx_start.value_or(0);
  // tx_end is the sample index where the signal ends, not a padding length.
  unsigned end_index = inmeta.tx_end.value_or(data.get_nof_samples());
  unsigned nsamples  = end_index - start_padding;

  // Flatten buffers.
  unsigned                                     nof_channels = data.get_nof_channels();
  static_vector<const ci16_t*, RADIO_MAX_NOF_CHANNELS> buffs_flat_ptr(nof_channels);
  for (unsigned ch = 0; ch < nof_channels; ++ch)
    buffs_flat_ptr[ch] = data.get_channel_buffer(ch).data() + start_padding;

  const lime::complex16_t* const* src = buffs_flat_ptr.data();

  lime::StreamTxMeta meta;
  meta.timestamp    = lime::Timespec(inmeta.ts + start_padding);
  meta.hasTimestamp = true;
  meta.flags        = inmeta.tx_end.has_value() ? lime::StreamTxMeta::EndOfBurst : 0;

  int samplesSent = LimePlugin_Write_complex16(context.get(), src, nsamples, portId, meta);
  if (samplesSent <= 0) {
    // error
  }
}

void radio_limesuiteng_tx_stream::start()
{
  do_work = true;
}

void radio_limesuiteng_tx_stream::stop()
{
  do_work = false;
}
