/**
 * ZuluIDE™ - Copyright (c) 2023 Rabbit Hole Computing™
 *
 * ZuluIDE™ firmware is licensed under the GPL version 3 or any later version.
 *
 * https://www.gnu.org/licenses/gpl-3.0.html
 * ----
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 **/

#include "fw_upgrade.h"
#include <hardware/sync.h>
#include <hardware/flash.h>
#include <hardware/structs/scb.h>
#include <pico/multicore.h>
#include "lwip/apps/fs.h"
#include "lwip/apps/httpd.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/opt.h"
#include "boot/uf2.h"
#include <string.h>

#if PICO_RP2350
#define UF2_FAMILY_ID RP2350_ARM_S_FAMILY_ID
#else
#define UF2_FAMILY_ID RP2040_FAMILY_ID
#endif

// This is constant for RP2xxx
#define UF2_PAYLOAD_SIZE 256

// Uploaded data is temporarily stored at 1 MB offset from flash start.
// It is only flashed to main location after the whole image is successfully received.
#define FW_UPGRADE_TEMP_OFFSET (1024 * 1024)
#define FW_UPGRADE_TARGET_ADDR 0x10000000

// 4kB erase blocks are large enough for all flash chips
#define FLASH_SECTOR_ERASE_SIZE 4096u

static struct {
    size_t block_size;
    uf2_block block;
    uint32_t blocks_received;
    uint32_t num_blocks;
} g_fwup_state;

err_t fwupgrade_post_begin(void *connection, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len, char *response_uri,
                       u16_t response_uri_len, u8_t *post_auto_wnd)
{
    printf("fwupgrade_post_begin %s\n", uri);
    g_fwup_state.block_size = 0;
    g_fwup_state.blocks_received = 0;
    g_fwup_state.num_blocks = 0;
    return ERR_OK;
}

// Erase whole flash area before programming it
__attribute__((section(".time_critical.erase_flash_area")))
static void erase_flash_area(uint32_t offset, uint32_t len)
{
    if (len % FLASH_SECTOR_ERASE_SIZE != 0)
    {
        len += FLASH_SECTOR_ERASE_SIZE - (len % FLASH_SECTOR_ERASE_SIZE);
    }

    uint32_t saved_irq = save_and_disable_interrupts();
    flash_range_erase(offset, len);
    restore_interrupts(saved_irq);
}

// Program one flash block
__attribute__((section(".time_critical.program_flash_block")))
static bool program_flash_block(uint32_t offset, uint8_t *data)
{
    uint32_t saved_irq = save_and_disable_interrupts();
    flash_range_program(offset, data, UF2_PAYLOAD_SIZE);
    restore_interrupts(saved_irq);
    bool success = (memcmp(data, (void*)(XIP_NOCACHE_NOALLOC_BASE + offset), UF2_PAYLOAD_SIZE) == 0);
    return success;
}

// Copy firmware from temporary area to final flash location.
// Note: this must be fully in RAM and not call any flash functions,
// because the flash is being overwritten.
__attribute__((section(".time_critical.finish_fw_upgrade")))
int64_t finish_fw_upgrade(alarm_id_t id, void *user_data)
{
    uint32_t total_fw_size = g_fwup_state.num_blocks * UF2_PAYLOAD_SIZE;
    if (total_fw_size < 16 * 1024 ||
        total_fw_size > FW_UPGRADE_TEMP_OFFSET)
    {
        return 0; // Just a sanity check
    }

    save_and_disable_interrupts();

    // Copy firmware from temp area to the final offset
    erase_flash_area(0, total_fw_size);
    for (uint32_t block = 0; block < g_fwup_state.num_blocks; block++)
    {
        // We need to copy each block to RAM and from there back to flash
        // at the final location. But memcpy might not be in RAM, so do it manually.
        uint8_t buf[UF2_PAYLOAD_SIZE];
        uint32_t offset = block * UF2_PAYLOAD_SIZE;
        uint8_t *src = (uint8_t*)(FW_UPGRADE_TARGET_ADDR + FW_UPGRADE_TEMP_OFFSET + offset);
        for (size_t i = 0; i < UF2_PAYLOAD_SIZE; i++)
        {
            buf[i] = src[i];
        }

        flash_range_program(offset, buf, UF2_PAYLOAD_SIZE);
    }

    // Reboot
    scb_hw->aircr = 0x05FA0004;
    while(1);
}

static bool handle_uf2_block(uf2_block *block)
{
    if (block->magic_start0 != UF2_MAGIC_START0 ||
        block->magic_start1 != UF2_MAGIC_START1 ||
        block->magic_end != UF2_MAGIC_END)
    {
        printf("UF2 magics do not match (0x%08lx 0x%08lx 0x%08lx)\n",
            block->magic_start0, block->magic_start1, block->magic_end);
        return false;
    }

    if (block->file_size != UF2_FAMILY_ID || !(block->flags & UF2_FLAG_FAMILY_ID_PRESENT))
    {
        printf("Ignoring block for different family id (expected 0x%08x, got 0x%08lx)\n",
            UF2_FAMILY_ID, block->file_size);

        // Continue upload until we get a block for us in a universal binary
        return true;
    }

    if (block->flags & UF2_FLAG_NOT_MAIN_FLASH)
    {
        printf("Ignoring not-for-flash UF2 block\n");
        return true;
    }

    if (block->payload_size != UF2_PAYLOAD_SIZE)
    {
        printf("Unexpected payload size %lu\n", block->payload_size);
        return false;
    }

    if (block->target_addr < FW_UPGRADE_TARGET_ADDR ||
        block->target_addr >= FW_UPGRADE_TARGET_ADDR + FW_UPGRADE_TEMP_OFFSET)
    {
        printf("UF2 block offset out of range: 0x%08lx\n", block->target_addr);
        return false;
    }

    if (block->block_no == 0)
    {
        printf("Got first UF2 block, total %lu blocks\n", block->num_blocks);
        g_fwup_state.blocks_received = 0;
        g_fwup_state.num_blocks = block->num_blocks;

        printf("Stopping second core");
        multicore_reset_core1();

        uint32_t total_bytes = block->num_blocks * UF2_PAYLOAD_SIZE;
        printf("Erasing temp area for %lu bytes\n", total_bytes);
        erase_flash_area(FW_UPGRADE_TEMP_OFFSET, total_bytes);
    }
    else if (block->block_no != g_fwup_state.blocks_received)
    {
        printf("UF2 block out of order, got %lu expected %lu\n",
            block->block_no, g_fwup_state.blocks_received);
        return false;
    }

    uint32_t block_offset = block->target_addr - FW_UPGRADE_TARGET_ADDR;
    uint32_t tmp_addr = FW_UPGRADE_TEMP_OFFSET + block_offset;
    printf("Programming UF2 block %lu/%lu to %lu\n", block->block_no, block->num_blocks, tmp_addr);
    if (!program_flash_block(tmp_addr, block->data))
    {
        printf("Programming UF2 block to temporary flash failed at addr %lu\n", tmp_addr);
        return false;
    }
    printf("Block programming successful\n");
    g_fwup_state.blocks_received++;

    return true;
}

err_t fwupgrade_post_receive_data(void *connection, struct pbuf *p)
{
    uint8_t *data = (uint8_t*)p->payload;
    printf("fwupgrade_post_receive_data %d 0x%02x 0x%02x\n", (int)p->len, data[0], data[1]);

    // Process one UF2 block at a time.
    // For RP2xxx the UF2 blocks are always 512 bytes in size.
    size_t remain = p->len;
    while (remain > 0)
    {
        size_t block_remain = sizeof(uf2_block) - g_fwup_state.block_size;
        size_t to_cpy = (remain > block_remain) ? block_remain : remain;
        memcpy((uint8_t*)&g_fwup_state.block + g_fwup_state.block_size, data, to_cpy);
        g_fwup_state.block_size += to_cpy;
        data += to_cpy;
        remain -= to_cpy;

        if (g_fwup_state.block_size == sizeof(uf2_block))
        {
            if (!handle_uf2_block(&g_fwup_state.block))
            {
                printf("handle_uf2_block() failed\n");
                return ERR_VAL;
            }
            g_fwup_state.block_size = 0;
        }
    }

    pbuf_free(p);
    return ERR_OK;
}

void start_multicore_i2c();

void fwupgrade_post_finished(void *connection, char *response_uri, u16_t response_uri_len)
{
    if (g_fwup_state.num_blocks == 0 ||
        g_fwup_state.blocks_received != g_fwup_state.num_blocks)
    {
        printf("fwupgrade interrupted, %lu/%lu blocks done\n",
            g_fwup_state.blocks_received, g_fwup_state.num_blocks);

        if (g_fwup_state.num_blocks > 0)
        {
            // We reset multicore so restore it back to operation
            start_multicore_i2c();
        }
    }
    else
    {
        printf("fwupgrade_post_finished\n");
        snprintf(response_uri, response_uri_len, "/fw_upgrade.html");

        // Let lwip post the result and then proceed to copy the firmware to the actual
        // location and reboot.
        add_alarm_in_ms(500, finish_fw_upgrade, NULL, false);
    }
}
