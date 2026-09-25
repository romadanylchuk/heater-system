/* home-heating project widgets (stage 05 placeholder). The Status page renders
 * each slot as an empty .slot container and calls PROJECT.render(slotId, el,
 * state) inside try/catch. Stage 08 fills the slots (heating circuits, pumps).
 * Project-specific strings go in lang.en / lang.uk (merged over the common
 * language files by the SPA). */
'use strict';

window.PROJECT = {
  name: 'home-heating',
  slots: [
    { id: 'main', titleKey: 'status.slot.main' },
    { id: 'aux', titleKey: 'status.slot.aux' }
  ],
  render: function (slotId, el, state) {
    /* stage 08 */
  },
  lang: {
    en: { 'status.slot.main': 'Home heating', 'status.slot.aux': 'Details' },
    uk: { 'status.slot.main': 'Опалення будинку', 'status.slot.aux': 'Подробиці' }
  }
};
