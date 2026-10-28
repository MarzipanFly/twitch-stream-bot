#!/usr/bin/env python3
"""Experimental QLoRA SFT for ApelsinBot. Run in Linux/WSL2 with a CUDA GPU."""
import argparse
import sys
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model', default='Qwen/Qwen3-4B-Instruct-2507', help='Hugging Face base checkpoint; verify it matches the Ollama model family')
    parser.add_argument('--data-dir', type=Path, default=Path(__file__).resolve().parent / 'generated')
    parser.add_argument('--output-dir', type=Path, default=Path(__file__).resolve().parent / 'adapters' / 'apelsinbot-lora')
    parser.add_argument('--epochs', type=float, default=3.0)
    parser.add_argument('--max-length', type=int, default=1024)
    args = parser.parse_args()
    train_file = args.data_dir / 'sft_train.jsonl'
    eval_file = args.data_dir / 'sft_validation.jsonl'
    if not train_file.is_file() or not eval_file.is_file():
        parser.error('Run python training/prepare_sft.py first.')
    if args.max_length < 256 or args.max_length > 2048:
        parser.error('--max-length must be between 256 and 2048 for this starter profile.')
    try:
        import torch
        from datasets import load_dataset
        from peft import LoraConfig
        from transformers import AutoModelForCausalLM, AutoTokenizer, BitsAndBytesConfig
        from trl import SFTConfig, SFTTrainer
    except ImportError as exc:
        print('Missing training dependency: %s' % exc, file=sys.stderr)
        print('Install training/requirements-lora.txt inside WSL2/Linux.', file=sys.stderr)
        return 2
    if not torch.cuda.is_available():
        print('CUDA GPU not found. This QLoRA profile is intended for a CUDA GPU.', file=sys.stderr)
        return 2
    use_bf16 = bool(hasattr(torch.cuda, 'is_bf16_supported') and torch.cuda.is_bf16_supported())
    compute_dtype = torch.bfloat16 if use_bf16 else torch.float16
    quantization = BitsAndBytesConfig(load_in_4bit=True, bnb_4bit_quant_type='nf4', bnb_4bit_use_double_quant=True, bnb_4bit_compute_dtype=compute_dtype)
    print('Loading base model:', args.model)
    tokenizer = AutoTokenizer.from_pretrained(args.model)
    if tokenizer.pad_token is None:
        tokenizer.pad_token = tokenizer.eos_token
    model = AutoModelForCausalLM.from_pretrained(args.model, quantization_config=quantization, device_map='auto', torch_dtype=compute_dtype)
    model.config.use_cache = False
    dataset = load_dataset('json', data_files={'train': str(train_file), 'validation': str(eval_file)})
    peft = LoraConfig(r=16, lora_alpha=32, lora_dropout=0.05, bias='none', task_type='CAUSAL_LM', target_modules=['q_proj','k_proj','v_proj','o_proj','gate_proj','up_proj','down_proj'])
    training_args = SFTConfig(output_dir=str(args.output_dir), num_train_epochs=args.epochs, per_device_train_batch_size=1, per_device_eval_batch_size=1, gradient_accumulation_steps=8, learning_rate=1e-4, warmup_ratio=0.05, weight_decay=0.01, logging_steps=5, eval_strategy='epoch', save_strategy='epoch', save_total_limit=2, load_best_model_at_end=True, metric_for_best_model='eval_loss', greater_is_better=False, gradient_checkpointing=True, gradient_checkpointing_kwargs={'use_reentrant': False}, optim='paged_adamw_8bit', max_length=args.max_length, packing=False, assistant_only_loss=True, bf16=use_bf16, fp16=not use_bf16, report_to='none', seed=20261028)
    trainer = SFTTrainer(model=model, args=training_args, train_dataset=dataset['train'], eval_dataset=dataset['validation'], processing_class=tokenizer, peft_config=peft)
    trainer.train()
    trainer.save_model(str(args.output_dir))
    tokenizer.save_pretrained(str(args.output_dir))
    print('LoRA adapter saved to:', args.output_dir)
    print('This is not yet an Ollama model. Evaluate the adapter before merging/exporting.')
    return 0

if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as exc:
        print('QLoRA training failed: %s' % exc, file=sys.stderr)
        raise
