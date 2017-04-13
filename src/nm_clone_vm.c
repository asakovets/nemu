#include <nm_core.h>
#include <nm_form.h>
#include <nm_utils.h>
#include <nm_string.h>
#include <nm_window.h>
#include <nm_network.h>
#include <nm_database.h>
#include <nm_cfg_file.h>
#include <nm_vm_control.h>

static void nm_clone_vm_to_fs(const nm_str_t *src, const nm_str_t *dst,
                              const nm_vect_t *drives);
static void nm_clone_vm_to_db(const nm_str_t *src, const nm_str_t *dst,
                              const nm_vmctl_data_t *vm);

void nm_clone_vm(const nm_str_t *name)
{
    nm_form_t *form = NULL;
    nm_field_t *fields[2];
    nm_window_t *window = NULL;
    nm_vmctl_data_t vm = NM_VMCTL_INIT_DATA;
    nm_str_t buf = NM_INIT_STR;
    nm_str_t cl_name = NM_INIT_STR;
    nm_spinner_data_t sp_data = NM_INIT_SPINNER;
    nm_vect_t err = NM_INIT_VECT;
    size_t msg_len;
    pthread_t spin_th;
    int done = 0;

    nm_vmctl_get_data(name, &vm);

    nm_print_title(_(NM_EDIT_TITLE));
    window = nm_init_window(7, 45, 3);

    init_pair(1, COLOR_BLACK, COLOR_WHITE);
    wbkgd(window, COLOR_PAIR(1));

    fields[0] = new_field(1, 30, 2, 1, 0, 0);
    set_field_back(fields[0], A_UNDERLINE);
    fields[1] = NULL;

    set_field_type(fields[0], TYPE_REGEXP, "^[a-zA-Z0-9_-]{1,30} *$");

    nm_str_alloc_str(&buf, name);
    nm_str_add_char(&buf, '_');
    set_field_buffer(fields[0], 0, buf.data);
    nm_str_trunc(&buf, 0);

    nm_str_add_text(&buf, _("Clone "));
    nm_str_add_str(&buf, name);
    mvwaddstr(window, 1, 2, buf.data);
    mvwaddstr(window, 4, 2, _("Name"));
    nm_str_trunc(&buf, 0);

    form = nm_post_form(window, fields, 10);
    if (nm_draw_form(window, form) != NM_OK)
        goto out;

    nm_get_field_buf(fields[0], &cl_name);
    nm_form_check_data(_("Name"), cl_name, err);

    if (nm_print_empty_fields(&err) == NM_ERR)
    {
        nm_vect_free(&err, NULL);
        goto out;
    }

    if (nm_form_name_used(&cl_name) == NM_ERR)
        goto out;

    msg_len = mbstowcs(NULL, _(NM_EDIT_TITLE), strlen(_(NM_EDIT_TITLE)));
    sp_data.stop = &done;
    sp_data.x = (getmaxx(stdscr) + msg_len + 2) / 2;

    if (pthread_create(&spin_th, NULL, nm_spinner, (void *) &sp_data) != 0)
        nm_bug(_("%s: cannot create thread"), __func__);

    nm_clone_vm_to_fs(name, &cl_name, &vm.drives);
    nm_clone_vm_to_db(name, &cl_name, &vm);

    done = 1;
    if (pthread_join(spin_th, NULL) != 0)
        nm_bug(_("%s: cannot join thread"), __func__);

out:
    nm_vmctl_free_data(&vm);
    nm_form_free(form, fields);
    nm_str_free(&buf);
    nm_str_free(&cl_name);
}

static void nm_clone_vm_to_fs(const nm_str_t *src, const nm_str_t *dst,
                              const nm_vect_t *drives)
{
    nm_str_t old_vm_path = NM_INIT_STR;
    nm_str_t new_vm_path = NM_INIT_STR;
    nm_str_t new_vm_dir = NM_INIT_STR;
    size_t drives_count;
    char drv_ch = 'a';

    nm_str_copy(&new_vm_dir, &nm_cfg_get()->vm_dir);
    nm_str_add_char(&new_vm_dir, '/');
    nm_str_add_str(&new_vm_dir, dst);

    if (mkdir(new_vm_dir.data, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0)
    {
        nm_bug(_("%s: cannot create VM directory %s: %s"),
               __func__, new_vm_dir.data, strerror(errno));
    }

    nm_str_copy(&new_vm_path, &nm_cfg_get()->vm_dir);
    nm_str_copy(&old_vm_path, &nm_cfg_get()->vm_dir);
    nm_str_format(&new_vm_path, "/%s/%s", dst->data, dst->data);
    nm_str_format(&old_vm_path, "/%s/", src->data);

    drives_count = drives->n_memb / 4;

    for (size_t n = 0; n < drives_count; n++)
    {
        size_t idx_shift = 4 * n;
        nm_str_t *drive_name = nm_vect_str(drives, NM_SQL_DRV_NAME + idx_shift);

        nm_str_add_str(&old_vm_path, drive_name);
        nm_str_format(&new_vm_path, "_%c.img", drv_ch);

        nm_copy_file(&old_vm_path, &new_vm_path);

        nm_str_trunc(&old_vm_path, old_vm_path.len - drive_name->len);
        nm_str_trunc(&new_vm_path, new_vm_path.len - 6);
        drv_ch++;
    }

    nm_str_free(&old_vm_path);
    nm_str_free(&new_vm_path);
    nm_str_free(&new_vm_dir);
}

static void nm_clone_vm_to_db(const nm_str_t *src, const nm_str_t *dst,
                              const nm_vmctl_data_t *vm)
{
    nm_str_t query = NM_INIT_STR;
    uint64_t last_mac;
    uint32_t last_vnc;
    size_t ifs_count;
    size_t drives_count;
    char drv_ch = 'a';

    nm_form_get_last(&last_mac, &last_vnc);

    nm_str_alloc_text(&query, "INSERT INTO vms SELECT NULL, '");
    nm_str_add_str(&query, dst);
    nm_str_add_text(&query, "', mem, smp, kvm, hcpu, '");
    nm_str_format(&query, "%d", last_vnc);
    nm_str_add_text(&query, "', arch, iso, install, usb, "
        "usbid, bios, kernel, mouse_override, kernel_append, tty_path, socket_path"
        " FROM vms WHERE name='");
    nm_str_add_str(&query, src);
    nm_str_add_char(&query, '\'');

    nm_db_edit(query.data);

    /* {{{ insert network interface info */
    ifs_count = vm->ifs.n_memb / 4;

    for (size_t n = 0; n < ifs_count; n++)
    {
        size_t idx_shift = 4 * n;
        nm_str_t if_name = NM_INIT_STR;
        nm_str_t maddr = NM_INIT_STR;
        nm_str_trunc(&query, 0);
        last_mac++;

        nm_net_mac_to_str(last_mac, &maddr);

        nm_str_format(&if_name, "%s_eth%zu", dst->data, n);
        if (if_name.len > 15) /* Linux tap iface max name len */
        {
            nm_str_trunc(&if_name, 14);
            nm_str_format(&if_name, "%zu", n);
        }

        nm_str_add_text(&query, "INSERT INTO ifaces("
            "vm_name, if_name, mac_addr, if_drv) VALUES('");
        nm_str_add_str(&query, dst);
        nm_str_add_text(&query, "', '");
        nm_str_add_str(&query, &if_name);
        nm_str_add_text(&query, "', '");
        nm_str_add_str(&query, &maddr);
        nm_str_add_text(&query, "', '");
        nm_str_add_str(&query, nm_vect_str(&vm->ifs, NM_SQL_IF_DRV + idx_shift));
        nm_str_add_text(&query, "')");

        nm_db_edit(query.data);

        nm_str_free(&if_name);
        nm_str_free(&maddr);
    } /* }}} network */

    /* {{{ insert drive info */
    drives_count = vm->drives.n_memb / 4;

    for (size_t n = 0; n < drives_count; n++)
    {
        size_t idx_shift = 4 * n;

        nm_str_trunc(&query, 0);
        nm_str_add_text(&query, "INSERT INTO drives("
            "vm_name, drive_name, drive_drv, capacity, boot) VALUES('");
        nm_str_add_str(&query, dst);
        nm_str_add_text(&query, "', '");
        nm_str_add_str(&query, dst);
        nm_str_format(&query, "_%c.img", drv_ch);
        nm_str_add_text(&query, "', '");
        nm_str_add_str(&query, nm_vect_str(&vm->drives, NM_SQL_DRV_TYPE + idx_shift));
        nm_str_add_text(&query, "', '");
        nm_str_add_str(&query, nm_vect_str(&vm->drives, NM_SQL_DRV_SIZE + idx_shift));
        nm_str_add_text(&query, "', '");
        nm_str_add_str(&query, nm_vect_str(&vm->drives, NM_SQL_DRV_BOOT + idx_shift));
        nm_str_add_text(&query, "')");

        nm_db_edit(query.data);

        drv_ch++;
    } /* }}} drives */

    nm_form_update_last_mac(last_mac);
    nm_form_update_last_vnc(last_vnc);

    nm_str_free(&query);
}

/* vim:set ts=4 sw=4 fdm=marker: */