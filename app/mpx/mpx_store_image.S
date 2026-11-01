/* 只嵌入 Tomb Explorer。改 bin 后要动这个文件，incbin 才会重编。 */
.section .rodata
.global mpx_store_image
.global mpx_store_image_end
.align 4
mpx_store_image:
.incbin "H:/jl/fw-mini-sdk/app/mpx/mpx_store_image.bin"
.align 4
mpx_store_image_end:
