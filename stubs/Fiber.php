<?php
final class Fiber {
	public function __construct(callable $callback) {
	}
	public function start(...$args) {
	}
	public function resume($value = null) {
	}
	public function throw(Throwable $exception) {
	}
	public function getReturn() {
	}
	public function isStarted(): bool {
	}
	public function isSuspended(): bool {
	}
	public function isRunning(): bool {
	}
	public function isTerminated(): bool {
	}
	public static function suspend($value = null) {
	}
	public static function getCurrent(): ?Fiber {
	}
}