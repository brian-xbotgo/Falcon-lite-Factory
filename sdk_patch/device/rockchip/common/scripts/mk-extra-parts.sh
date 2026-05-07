#!/bin/bash -e

# Hooks

usage_hook()
{
	usage_oneline "extra-parts" "pack extra partition images"
}

clean_hook()
{
	rm -rf "$RK_EXTRA_PART_OUTDIR"

	for idx in $(seq 1 "$(rk_extra_part_num)"); do
		IMAGE="$(basename "$(rk_extra_part_img $idx)")"
		rm -rf "$RK_FIRMWARE_DIR/$IMAGE"
	done
}

POST_BUILD_CMDS="extra-parts"
post_build_hook()
{
	"$RK_SCRIPTS_DIR/check-package.sh" fakeroot

	for idx in $(seq 1 "$(rk_extra_part_num)"); do
		PART_NAME="$(rk_extra_part_name $idx)"
		FS_TYPE="$(rk_extra_part_fstype $idx)"
		SIZE="$(rk_extra_part_size $idx)"
		OPTIONS="$(rk_extra_part_mkfs_opts $idx)"
		FAKEROOT_SCRIPT="$(rk_extra_part_fakeroot_script $idx)"
		OUTDIR="$(rk_extra_part_outdir $idx)"
		DST="$(rk_extra_part_img $idx)"

		rk_extra_part_prepare $idx

		if rk_extra_part_builtin $idx; then
			notice "Skip packing $PART_NAME (builtin)"
			continue
		fi

		if [ "$FS_TYPE" = "ubifs" ] && [ "$SIZE" = auto ]; then
			SIZE=max
		fi

		if [ "$SIZE" = max ]; then
			SIZE="$(rk_partition_size_kb "$PART_NAME")K"
			if [ "$SIZE" = 0K ]; then
				if [ "$FS_TYPE" != "ubifs" ]; then
					error "Unable to detect max size of $PART_NAME"
					return 1
				fi

				SIZE="${RK_FLASH_SIZE}M"
				notice "Flash storage size is $SIZE"
			fi

			notice "Using maxium size($SIZE) for $PART_NAME"
		fi

		if [ "$PART_NAME" = "oem" ]; then
                        if [ "$SIZE" != "max" ] && [ "$SIZE" != "auto" ]; then
                                num=${SIZE%[KM]}  
                                unit=${SIZE: -1}  
                                if [ "$unit" = "M" ]; then
                                        new_num=$((num * 1024 + 51200))
                                        SIZE="$((new_num / 1024))M"
                                elif [ "$unit" = "K" ]; then
                                        SIZE="$((num + 51200))K"
                                fi
                                notice "Adjusted $PART_NAME size to $SIZE (added 50MB)"
                        elif [ "$SIZE" = "auto" ]; then
                                actual_size=$(du -s "$OUTDIR" | awk '{print $1}')
                                SIZE="$((actual_size + 51200))K"
                                notice "Auto-adjusted $PART_NAME size to $SIZE (added 50MB)"
                        fi
                fi

		echo "find \"$OUTDIR\" -user $RK_OWNER_UID \
			-exec chown -ch 0:0 {} \\;" >> "$FAKEROOT_SCRIPT"

		# The mk-image.sh expects ubi-<type> to pack ubi image.
		if [ "$RK_UBI" ]; then
			FS_TYPE="ubi-$FS_TYPE"
		fi

		sed -i '/mk-image.sh/d' "$FAKEROOT_SCRIPT"
		echo "\"$RK_SCRIPTS_DIR/mk-image.sh\" \
			-t \"$FS_TYPE\" -s \"$SIZE\" -l \"$PART_NAME\" -o \"$OPTIONS\" \
			\"$OUTDIR\" \"$DST\"" >> "$FAKEROOT_SCRIPT"

		notice "Packing $DST from $FAKEROOT_SCRIPT"
		cd "$OUTDIR"
		fakeroot -- "$FAKEROOT_SCRIPT"
		notice "Done packing $DST"

		ln -rsf "$DST" "$RK_FIRMWARE_DIR/"

		if ! rk_partition_parse_names | grep -qE "\<$PART_NAME\>"; then
			warning "Packed $DST without having $PART_NAME partition!"
		fi
	done

	finish_build build_extra_part
}

source "${RK_BUILD_HELPER:-$(dirname "$(realpath "$0")")/build-helper}"

post_build_hook
