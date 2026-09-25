/* boiler-room project widgets (stage 05 placeholder). The Status page renders
 * each slot as an empty .slot container and calls PROJECT.render(slotId, el,
 * state) inside try/catch. Stage 07 fills the slots (accumulator, K1, pumps).
 * Project-specific strings go in lang.en / lang.uk (merged over the common
 * language files by the SPA). */
'use strict';

window.PROJECT = {
  name: 'boiler-room',
  slots: [
    { id: 'main', titleKey: 'status.slot.main' },
    { id: 'aux', titleKey: 'status.slot.aux' }
  ],
  render: function (slotId, el, state) {
    /* stage 07 */
  },
  lang: {
    en: { 'status.slot.main': 'Boiler room', 'status.slot.aux': 'Details' },
    uk: { 'status.slot.main': 'Котельня', 'status.slot.aux': 'Подробиці' }
  }
};
